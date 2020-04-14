use anyhow::{bail, ensure, Context, Result};
use std::collections::HashSet;
use std::ffi::{OsStr, OsString};
use std::fs::{self, File};
use std::io::{self, Read};
use std::os::unix::process::ExitStatusExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};

const TIMEOUT: u32 = 90;

/// Replace the first '@@' in the given command line with the input file.
fn insert_input_file<S: AsRef<OsStr>, P: AsRef<Path>>(
    command: &[S],
    input_file: P,
) -> Vec<OsString> {
    let mut fixed_command: Vec<OsString> = command.iter().map(|s| s.into()).collect();
    if let Some(at_signs) = fixed_command.iter_mut().find(|s| *s == "@@") {
        *at_signs = input_file.as_ref().as_os_str().to_os_string();
    }

    fixed_command
}

/// A coverage map as used by AFL.
pub struct AflMap {
    data: [u8; 65536],
}

impl AflMap {
    /// Create an empty map.
    pub fn new() -> AflMap {
        AflMap { data: [0; 65536] }
    }

    /// Load a map from disk.
    pub fn load(path: impl AsRef<Path>) -> Result<AflMap> {
        let data = fs::read(&path).with_context(|| {
            format!(
                "Failed to read the AFL bitmap that \
                 afl-showmap should have generated at {}",
                path.as_ref().display()
            )
        })?;
        ensure!(
            data.len() == 65536,
            "The file to load the coverage map from has the wrong size ({})",
            data.len()
        );

        let mut result = AflMap::new();
        result.data.copy_from_slice(&data);
        Ok(result)
    }

    /// Merge with another coverage map in place.
    ///
    /// Return true if the map has changed, i.e., if the other map yielded new
    /// coverage.
    pub fn merge(&mut self, other: &AflMap) -> bool {
        let mut interesting = false;
        for (known, new) in self.data.iter_mut().zip(other.data.iter()) {
            if *known != (*known | new) {
                *known |= new;
                interesting = true;
            }
        }

        interesting
    }
}

/// Score of a test case.
///
/// We use the lexical comparison implemented by the derived implementation of
/// Ord in order to compare according to various criteria.
#[derive(PartialEq, Eq, PartialOrd, Ord, Debug)]
struct TestcaseScore {
    /// First criterion: new coverage
    new_coverage: bool,

    /// Second criterion: being derived from seed inputs
    derived_from_seed: bool,

    /// Third criterion: size (smaller is better)
    file_size: i128,

    /// Fourth criterion: name (containing the ID)
    base_name: OsString,
}

impl TestcaseScore {
    /// Score a test case.
    ///
    /// If anything goes wrong, return the minimum score.
    fn new(t: impl AsRef<Path>) -> Self {
        let size = match fs::metadata(&t) {
            Err(e) => {
                // Has the file disappeared?
                log::warn!(
                    "Warning: failed to score test case {}: {}",
                    t.as_ref().display(),
                    e
                );

                return TestcaseScore::minimum();
            }
            Ok(meta) => meta.len(),
        };

        let name: OsString = match t.as_ref().file_name() {
            None => return TestcaseScore::minimum(),
            Some(n) => n.to_os_string(),
        };
        let name_string = name.to_string_lossy();

        TestcaseScore {
            new_coverage: name_string.ends_with("+cov"),
            derived_from_seed: name_string.contains("orig:"),
            file_size: i128::from(size) * -1,
            base_name: name,
        }
    }

    /// Return the smallest possible score.
    fn minimum() -> TestcaseScore {
        TestcaseScore {
            new_coverage: false,
            derived_from_seed: false,
            file_size: std::i128::MIN,
            base_name: OsString::from(""),
        }
    }
}

/// A directory that we can write test cases to.
pub struct TestcaseDir {
    /// The path to the (existing) directory.
    pub path: PathBuf,
    /// The next free ID in this directory.
    current_id: u64,
}

impl TestcaseDir {
    /// Create a new test-case directory in the specified location.
    ///
    /// The parent directory must exist.
    pub fn new(path: impl AsRef<Path>) -> Result<TestcaseDir> {
        let dir = TestcaseDir {
            path: path.as_ref().into(),
            current_id: 0,
        };

        fs::create_dir(&dir.path)
            .with_context(|| format!("Failed to create directory {}", dir.path.display()))?;
        Ok(dir)
    }
}

/// Copy a test case to a directory, using the parent test case's name to derive
/// the new name.
pub fn copy_testcase(
    testcase: impl AsRef<Path>,
    target_dir: &mut TestcaseDir,
    parent: impl AsRef<Path>,
) -> Result<()> {
    let orig_name = parent
        .as_ref()
        .file_name()
        .expect("The input file does not have a name")
        .to_string_lossy();
    ensure!(
        orig_name.starts_with("id:"),
        "The name of test case {} does not start with an ID",
        parent.as_ref().display()
    );

    if let Some(orig_id) = orig_name.get(3..9) {
        let new_name = format!("id:{:06},src:{}", target_dir.current_id, &orig_id);
        let target = target_dir.path.join(new_name);
        log::debug!("Creating test case {}", target.display());
        fs::copy(testcase.as_ref(), target).with_context(|| {
            format!(
                "Failed to copy the test case {} to {}",
                testcase.as_ref().display(),
                target_dir.path.display()
            )
        })?;

        target_dir.current_id += 1;
    } else {
        bail!(
            "Test case {} does not contain a proper ID",
            parent.as_ref().display()
        );
    }

    Ok(())
}

/// Information on the run-time environment.
///
/// This should not change during execution.
#[derive(Debug)]
pub struct AflConfig {
    /// The location of the afl-showmap program.
    show_map: PathBuf,

    /// The command that AFL uses to invoke the target program.
    target_command: Vec<OsString>,

    /// Do we need to pass data to standard input?
    use_standard_input: bool,

    /// The fuzzer instance's queue of test cases.
    queue: PathBuf,
}

/// Possible results of afl-showmap.
pub enum AflShowmapResult {
    /// The map was created successfully.
    Success(AflMap),
    /// The target timed out or failed to execute.
    Hang,
    /// The target crashed.
    Crash,
}

impl AflConfig {
    /// Read the AFL configuration from a fuzzer instance's output directory.
    pub fn load(fuzzer_output: impl AsRef<Path>) -> Result<Self> {
        let afl_stats_file_path = fuzzer_output.as_ref().join("fuzzer_stats");
        let mut afl_stats_file = File::open(&afl_stats_file_path).with_context(|| {
            format!(
                "Failed to open the fuzzer's stats at {}",
                afl_stats_file_path.display()
            )
        })?;
        let mut afl_stats = String::new();
        afl_stats_file
            .read_to_string(&mut afl_stats)
            .with_context(|| {
                format!(
                    "Failed to read the fuzzer's stats at {}",
                    afl_stats_file_path.display()
                )
            })?;
        let afl_command: Vec<_> = afl_stats
            .lines()
            .find(|&l| l.starts_with("command_line"))
            .expect("The fuzzer stats don't contain the command line")
            .splitn(2, ':')
            .skip(1)
            .next()
            .expect("The fuzzer stats follow an unknown format")
            .trim()
            .split_whitespace()
            .collect();
        let afl_target_command: Vec<_> = afl_command
            .iter()
            .skip_while(|s| **s != "--")
            .map(|s| OsString::from(s))
            .collect();
        let afl_binary_dir = Path::new(
            afl_command
                .get(0)
                .expect("The AFL command is unexpectedly short"),
        )
        .parent()
        .unwrap();

        Ok(AflConfig {
            show_map: afl_binary_dir.join("afl-showmap"),
            use_standard_input: !afl_target_command.contains(&"@@".into()),
            target_command: afl_target_command,
            queue: fuzzer_output.as_ref().join("queue"),
        })
    }

    /// Return the most promising unseen test case of this fuzzer.
    pub fn best_new_testcase(&self, seen: &HashSet<PathBuf>) -> Result<Option<PathBuf>> {
        let best = fs::read_dir(&self.queue)
            .with_context(|| {
                format!(
                    "Failed to open the fuzzer's queue at {}",
                    self.queue.display()
                )
            })?
            .collect::<io::Result<Vec<_>>>()
            .with_context(|| {
                format!(
                    "Failed to read the fuzzer's queue at {}",
                    self.queue.display()
                )
            })?
            .into_iter()
            .map(|entry| entry.path())
            .filter(|path| path.is_file() && !seen.contains(path))
            .max_by_key(|path| TestcaseScore::new(path));

        Ok(best)
    }

    pub fn run_showmap(
        &self,
        testcase_bitmap: impl AsRef<Path>,
        testcase: impl AsRef<Path>,
    ) -> Result<AflShowmapResult> {
        let mut afl_show_map = Command::new(&self.show_map);
        afl_show_map
            .args(&["-t", "5000", "-m", "none", "-b", "-o"])
            .arg(testcase_bitmap.as_ref())
            .args(insert_input_file(&self.target_command, &testcase))
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .stdin(if self.use_standard_input {
                Stdio::piped()
            } else {
                Stdio::null()
            });

        log::debug!("Running afl-showmap as follows: {:?}", &afl_show_map);
        let mut afl_show_map_child = afl_show_map.spawn().context("Failed to run afl-showmap")?;

        if self.use_standard_input {
            io::copy(
                &mut File::open(&testcase)?,
                afl_show_map_child
                    .stdin
                    .as_mut()
                    .expect("Failed to open the stardard input of afl-showmap"),
            )
            .context("Failed to pipe the test input to afl-showmap")?;
        }

        let afl_show_map_status = afl_show_map_child
            .wait()
            .context("Failed to wait for afl-showmap")?;
        log::debug!("afl-showmap returned {}", &afl_show_map_status);
        match afl_show_map_status
            .code()
            .expect("No exit code available for afl-showmap")
        {
            0 => {
                let map = AflMap::load(&testcase_bitmap).with_context(|| {
                    format!(
                        "Failed to read the AFL bitmap that \
                         afl-showmap should have generated at {}",
                        testcase_bitmap.as_ref().display()
                    )
                })?;
                Ok(AflShowmapResult::Success(map))
            }
            1 => Ok(AflShowmapResult::Hang),
            2 => Ok(AflShowmapResult::Crash),
            unexpected => panic!("Unexpected return code {} from afl-showmap", unexpected),
        }
    }
}

/// The run-time configuration of SymCC.
#[derive(Debug)]
pub struct SymCC {
    /// Do we pass data to standard input?
    use_standard_input: bool,

    /// The cumulative bitmap for branch pruning.
    bitmap: PathBuf,

    /// The place to store the current input.
    input_file: PathBuf,

    /// The command to run.
    command: Vec<OsString>,
}

impl SymCC {
    /// Create a new SymCC configuration.
    pub fn new(output_dir: PathBuf, command: &Vec<String>) -> Self {
        let input_file = output_dir.join(".cur_input");

        SymCC {
            use_standard_input: !command.contains(&String::from("@@")),
            bitmap: output_dir.join("bitmap"),
            command: insert_input_file(command, &input_file),
            input_file: input_file,
        }
    }

    /// Run SymCC on the given input, writing results to the provided temporary
    /// directory. Return the list of newly generated test cases and a Boolean
    /// that indicates whether the target process was killed (i.e., possibly a
    /// timeout or out-of-memory condition).
    pub fn run(
        &self,
        input: impl AsRef<Path>,
        output_dir: impl AsRef<Path>,
    ) -> Result<(Vec<PathBuf>, bool)> {
        fs::copy(&input, &self.input_file).with_context(|| {
            format!(
                "Failed to copy the test case {} to our workbench at {}",
                input.as_ref().display(),
                self.input_file.display()
            )
        })?;

        fs::create_dir(&output_dir).with_context(|| {
            format!(
                "Failed to create the output directory {} for SymCC",
                output_dir.as_ref().display()
            )
        })?;

        let mut analysis_command = Command::new("timeout");
        analysis_command
            .args(&["-k", "5", &TIMEOUT.to_string()])
            .args(&self.command)
            .env("SYMCC_ENABLE_LINEARIZATION", "1")
            .env("SYMCC_AFL_COVERAGE_MAP", &self.bitmap)
            .env("SYMCC_OUTPUT_DIR", output_dir.as_ref())
            .stdout(Stdio::null())
            .stderr(Stdio::null());

        if self.use_standard_input {
            analysis_command.stdin(Stdio::piped());
        } else {
            analysis_command.stdin(Stdio::null());
            analysis_command.env("SYMCC_INPUT_FILE", &self.input_file);
        }

        log::debug!("Running SymCC as follows: {:?}", &analysis_command);
        let mut child = analysis_command.spawn().context("Failed to run SymCC")?;

        if self.use_standard_input {
            io::copy(
                &mut File::open(&self.input_file).with_context(|| {
                    format!(
                        "Failed to read the test input at {}",
                        self.input_file.display()
                    )
                })?,
                child
                    .stdin
                    .as_mut()
                    .expect("Failed to pipe to the child's standard input"),
            )
            .context("Failed to pipe the test input to SymCC")?;
        }

        let result = child.wait().context("Failed to wait for SymCC")?;
        let killed = match result.code() {
            Some(code) => {
                log::debug!("SymCC returned code {}", code);
                (code == 124) || (code == -9) // as per the man-page of timeout
            }
            None => {
                let maybe_sig = result.signal();
                if let Some(signal) = maybe_sig {
                    log::warn!("SymCC received signal {}", signal);
                }
                maybe_sig.is_some()
            }
        };

        let new_tests = fs::read_dir(&output_dir)
            .with_context(|| {
                format!(
                    "Failed to read the generated test cases at {}",
                    output_dir.as_ref().display()
                )
            })?
            .collect::<io::Result<Vec<_>>>()
            .with_context(|| {
                format!(
                    "Failed to read all test cases from {}",
                    output_dir.as_ref().display()
                )
            })?
            .iter()
            .map(|entry| entry.path())
            .collect();

        Ok((new_tests, killed))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_score_ordering() {
        let min_score = TestcaseScore::minimum();
        assert!(
            TestcaseScore {
                new_coverage: true,
                ..TestcaseScore::minimum()
            } > min_score
        );
        assert!(
            TestcaseScore {
                derived_from_seed: true,
                ..TestcaseScore::minimum()
            } > min_score
        );
        assert!(
            TestcaseScore {
                file_size: -4,
                ..TestcaseScore::minimum()
            } > min_score
        );
        assert!(
            TestcaseScore {
                base_name: OsString::from("foo"),
                ..TestcaseScore::minimum()
            } > min_score
        );
    }
}
