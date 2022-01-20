// This file is part of SymCC.
//
// SymCC is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// SymCC. If not, see <https://www.gnu.org/licenses/>.

use anyhow::{bail, ensure, Context, Result};
use regex::Regex;
use std::cmp;
use std::collections::HashSet;
use std::ffi::{OsStr, OsString};
use std::fs::{self, File};
use std::io::{self, Read};
use std::os::unix::process::ExitStatusExt;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::str;
use std::time::{Duration, Instant};

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
            file_size: -i128::from(size),
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

        fs::create_dir(&dir.path).with_context(|| {
            format!("Failed to create directory {}", dir.path.display())
        })?;
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
    pub show_map: PathBuf,

    /// The command that AFL uses to invoke the target program.
    pub target_command: Vec<OsString>,

    /// Do we need to pass data to standard input?
    pub use_standard_input: bool,

    /// Are we using AFL's QEMU mode?
    pub use_qemu_mode: bool,

    /// The fuzzer instance's queue of test cases.
    pub queue: PathBuf,
}

/// Possible results of afl-showmap.
pub enum AflShowmapResult {
    /// The map was created successfully.
    Success(Box<AflMap>),
    /// The target timed out or failed to execute.
    Hang,
    /// The target crashed.
    Crash,
    /// Ignore the afl-showmap result (e.g. on showmap errors)
    Ignore,
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
        afl_stats_file.read_to_string(&mut afl_stats).with_context(
            || {
                format!(
                    "Failed to read the fuzzer's stats at {}",
                    afl_stats_file_path.display()
                )
            },
        )?;
        let afl_command: Vec<_> = afl_stats
            .lines()
            .find(|&l| l.starts_with("command_line"))
            .expect("The fuzzer stats don't contain the command line")
            .splitn(2, ':')
            .nth(1)
            .expect("The fuzzer stats follow an unknown format")
            .trim()
            .split_whitespace()
            .collect();
        let afl_target_command: Vec<_> = afl_command
            .iter()
            .skip_while(|s| **s != "--")
            .map(OsString::from)
            .collect();
        let afl_binary_dir = Path::new(afl_command.get(0).expect(
            "The AFL command is unexpectedly short",
        )).parent()
            .unwrap();

        Ok(AflConfig {
            show_map: afl_binary_dir.join("afl-showmap"),
            use_standard_input: !afl_target_command.contains(&"@@".into()) &&
                !afl_command.contains(&"-f".into()),
            use_qemu_mode: afl_command.contains(&"-Q".into()),
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

        if self.use_qemu_mode {
            afl_show_map.arg("-Q");
        }

        afl_show_map
            .args(&["-t", "5000", "-m", "none", "-b", "-o"])
            .arg(testcase_bitmap.as_ref())
            .args(insert_input_file(&self.target_command, &testcase))
            .env("AFL_MAP_SIZE", "65535")  // req for afl++
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
                afl_show_map_child.stdin.as_mut().expect(
                    "Failed to open the stardard input of afl-showmap",
                ),
            ).context("Failed to pipe the test input to afl-showmap")?;
        }

        let afl_show_map_status = afl_show_map_child.wait().context(
            "Failed to wait for afl-showmap",
        )?;
        log::debug!("afl-showmap returned {}", &afl_show_map_status);
        match afl_show_map_status.code() {
            Some(0) => {
                let map = AflMap::load(&testcase_bitmap).with_context(|| {
                    format!(
                        "Failed to read the AFL bitmap that \
                         afl-showmap should have generated at {}",
                        testcase_bitmap.as_ref().display()
                    )
                });
                match map {
                    Ok(m) => Ok(AflShowmapResult::Success(Box::new(m))),
                    _ => Ok(AflShowmapResult::Ignore),
                }
            }
            Some(1) => Ok(AflShowmapResult::Hang),
            Some(2) => Ok(AflShowmapResult::Crash),
            _ => Ok(AflShowmapResult::Ignore),
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

    /// Actually use the bitmap?
    use_bitmap: bool,

    /// The place to store the current input.
    input_file: PathBuf,

    /// The command to run.
    command: Vec<OsString>,
}

/// The result of executing SymCC.
pub struct SymCCResult {
    /// The generated test cases.
    pub test_cases: Vec<PathBuf>,
    /// Whether the process was killed (e.g., out of memory, timeout).
    pub killed: bool,
    /// The total time taken by the execution.
    pub time: Duration,
    /// The time spent in the solver (Qsym backend only).
    pub solver_time: Option<Duration>,
}

impl SymCC {
    /// Create a new SymCC configuration.
    pub fn new(output_dir: PathBuf, command: &[String], use_bitmap: bool) -> Self {
        let input_file = output_dir.join(".cur_input");

        SymCC {
            use_standard_input: !command.contains(&String::from("@@")),
            bitmap: output_dir.join("bitmap"),
            use_bitmap,
            command: insert_input_file(command, &input_file),
            input_file,
        }
    }

    /// Try to extract the solver time from the logs produced by the Qsym
    /// backend.
    fn parse_solver_time(output: Vec<u8>) -> Option<Duration> {
        let re = Regex::new(r#""solving_time": (\d+)"#).unwrap();
        output
            // split into lines
            .rsplit(|n| *n == b'\n')
            // convert to string
            .filter_map(|s| str::from_utf8(s).ok())
            // check that it's an SMT log line
            .filter(|s| s.trim_start().starts_with("[STAT] SMT:"))
            // find the solving_time element
            .filter_map(|s| re.captures(s))
            // convert the time to an integer
            .filter_map(|c| c[1].parse().ok())
            // associate the integer with a unit
            .map(Duration::from_micros)
            // get the first one
            .next()
    }

    /// Run SymCC on the given input, writing results to the provided temporary
    /// directory.
    ///
    /// If SymCC is run with the Qsym backend, this function attempts to
    /// determine the time spent in the SMT solver and report it as part of the
    /// result. However, the mechanism that the backend uses to report solver
    /// time is somewhat brittle.
    pub fn run(
        &self,
        input: impl AsRef<Path>,
        output_dir: impl AsRef<Path>,
    ) -> Result<SymCCResult> {
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
            .args(&["-k", "15", &TIMEOUT.to_string()])
            .args(&self.command)
            .env("SYMCC_ENABLE_LINEARIZATION", "1")
            .env("SYMCC_AFL_COVERAGE_MAP", &self.bitmap)
            .env("SYMCC_OUTPUT_DIR", output_dir.as_ref())
            .stdout(Stdio::null())
            .stderr(Stdio::piped()); // capture SMT logs

        if self.use_bitmap {
            analysis_command.env("SYMCC_AFL_COVERAGE_MAP", &self.bitmap);
        }

        if self.use_standard_input {
            analysis_command.stdin(Stdio::piped());
        } else {
            analysis_command.stdin(Stdio::null());
            analysis_command.env("SYMCC_INPUT_FILE", &self.input_file);
        }

        log::debug!("Running SymCC as follows: {:?}", &analysis_command);
        let start = Instant::now();
        let mut child = analysis_command.spawn().context("Failed to run SymCC")?;

        if self.use_standard_input {
            io::copy(
                &mut File::open(&self.input_file).with_context(|| {
                    format!(
                        "Failed to read the test input at {}",
                        self.input_file.display()
                    )
                })?,
                child.stdin.as_mut().expect(
                    "Failed to pipe to the child's standard input",
                ),
            ).context("Failed to pipe the test input to SymCC")?;
        }

        let result = child.wait_with_output().context("Failed to wait for SymCC")?;
        let total_time = start.elapsed();
        let killed = match result.status.code() {
            Some(code) => {
                log::debug!("SymCC returned code {}", code);
                (code == 124) || (code == -9) // as per the man-page of timeout
            }
            None => {
                let maybe_sig = result.status.signal();
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

        let solver_time = SymCC::parse_solver_time(result.stderr);
        if solver_time.is_some() && solver_time.unwrap() > total_time {
            log::warn!("Backend reported inaccurate solver time!");
        }

        Ok(SymCCResult {
            test_cases: new_tests,
            killed,
            time: total_time,
            solver_time: solver_time.map(|t| cmp::min(t, total_time)),
        })
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

    #[test]
    fn test_solver_time_parsing() {
        let output = r#"[INFO] New testcase: /tmp/output/000005
[STAT] SMT: { "solving_time": 14539, "total_time": 185091 }
[STAT] SMT: { "solving_time": 14869 }
[STAT] SMT: { "solving_time": 14869, "total_time": 185742 }
[STAT] SMT: { "solving_time": 15106 }"#;

        assert_eq!(
            SymCC::parse_solver_time(output.as_bytes().to_vec()),
            Some(Duration::from_micros(15106))
        );
        assert_eq!(
            SymCC::parse_solver_time("whatever".as_bytes().to_vec()),
            None
        );
    }
}
