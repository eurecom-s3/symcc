use anyhow::{bail, ensure, Context, Result};
use std::collections::HashSet;
use std::ffi::{OsStr, OsString};
use std::fs::{self, File};
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::thread;
use std::time::Duration;
use structopt::StructOpt;
use tempfile::tempdir;

type AflMap = [u8; 65536];

const TIMEOUT: u32 = 90;

#[derive(Debug, StructOpt)]
#[structopt(about = "Make SymCC collaborate with AFL.", no_version)]
struct CLI {
    /// The name of the fuzzer to work with
    #[structopt(short = "a")]
    fuzzer_name: String,

    /// The AFL output directory
    #[structopt(short = "o")]
    output_dir: PathBuf,

    /// Name to use for SymCC
    #[structopt(short = "n")]
    name: String,

    /// Program under test
    command: Vec<String>,
}

fn main() -> Result<()> {
    let options = CLI::from_args();
    if !options.output_dir.is_dir() {
        eprintln!(
            "The directory {} does not exist!",
            options.output_dir.display()
        );
        return Ok(());
    }

    let afl_queue = options.output_dir.join(&options.fuzzer_name).join("queue");
    if !afl_queue.is_dir() {
        eprintln!("The AFL queue {} does not exist!", afl_queue.display());
        return Ok(());
    }

    let symcc_dir = options.output_dir.join(&options.name);
    if symcc_dir.is_dir() {
        eprintln!(
            "{} already exists; we do not currently support resuming",
            symcc_dir.display()
        );
        return Ok(());
    }

    let symcc = SymCC::new(symcc_dir.clone(), &options.command);
    let afl_config = AflConfig::load(options.output_dir.join(&options.fuzzer_name))?;
    let mut state = State::initialize(symcc_dir)?;

    loop {
        let test_files = afl_config
            .new_testcases(&state.processed_files)
            .context("Failed to check for new test cases")?;
        if test_files.is_empty() {
            println!("Waiting for new test cases...");
            thread::sleep(Duration::from_secs(5));
            continue;
        }

        for input in test_files {
            println!("Running on input {}", input.display());

            let tmp_dir = tempdir()
                .context("Failed to create a temporary directory for this execution of SymCC")?;

            let output_dir = symcc
                .run(&input, tmp_dir.path())
                .context("Failed to run SymCC")?;

            let mut num_interesting = 0u64;
            let mut num_total = 0u64;
            for maybe_new_test in fs::read_dir(&output_dir).with_context(|| {
                format!(
                    "Failed to read the generated test cases at {}",
                    output_dir.display()
                )
            })? {
                let new_test = maybe_new_test.with_context(|| {
                    format!(
                        "Failed to read all test cases from {}",
                        output_dir.display()
                    )
                })?;

                let res = process_new_testcase(
                    new_test.path(),
                    &input,
                    &tmp_dir,
                    &afl_config,
                    &mut state,
                )?;

                num_total += 1;
                if res == TestcaseResult::New {
                    num_interesting += 1;
                }
            }

            println!(
                "Generated {} test cases ({} new)",
                num_total, num_interesting
            );
            state.processed_files.insert(input);
        }
    }
}

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
    fn new<P: AsRef<Path>>(t: P) -> Self {
        let size = match fs::metadata(&t) {
            Err(e) => {
                // Has the file disappeared?
                eprintln!(
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
struct TestcaseDir {
    /// The path to the (existing) directory.
    path: PathBuf,
    /// The next free ID in this directory.
    current_id: u64,
}

/// Copy a test case to a directory, using the parent test case's name to derive
/// the new name.
fn copy_testcase<P: AsRef<Path>, Q: AsRef<Path>>(
    testcase: P,
    target_dir: &mut TestcaseDir,
    parent: Q,
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
        println!("Creating test case {}", target.display());
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
struct AflConfig {
    /// The location of the afl-showmap program.
    show_map: PathBuf,

    /// The command that AFL uses to invoke the target program.
    target_command: Vec<OsString>,

    /// Do we need to pass data to standard input?
    use_standard_input: bool,

    /// The fuzzer instance's queue of test cases.
    queue: PathBuf,
}

impl AflConfig {
    /// Read the AFL configuration from a fuzzer instance's output directory.
    fn load<P: AsRef<Path>>(fuzzer_output: P) -> Result<Self> {
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

    /// Return the unseen test cases of this fuzzer, ordered by descending
    /// relevance.
    fn new_testcases(&self, seen: &HashSet<PathBuf>) -> Result<Vec<PathBuf>> {
        let mut test_files: Vec<_> = fs::read_dir(&self.queue)
            .with_context(|| {
                format!(
                    "Failed to open the fuzzer's queue at {}",
                    self.queue.display()
                )
            })?
            .map(|entry| entry.map(|e| e.path()))
            .collect::<io::Result<Vec<_>>>()
            .with_context(|| {
                format!(
                    "Failed to read the fuzzer's queue at {}",
                    self.queue.display()
                )
            })?
            .into_iter()
            .filter(|path| path.is_file() && !seen.contains(path))
            .collect();

        test_files.sort_by_cached_key(|path| TestcaseScore::new(path));
        test_files.reverse();
        Ok(test_files)
    }
}

/// Mutable run-time state.
///
/// This is a collection of the state we update during execution.
struct State {
    /// The cumulative coverage of all test cases generated so far.
    current_bitmap: AflMap,

    /// The AFL test cases that have been analyzed so far.
    processed_files: HashSet<PathBuf>,

    /// The place to put new and useful test cases.
    queue: TestcaseDir,

    /// The place for new test cases that time out.
    hangs: TestcaseDir,

    /// The place for new test cases that crash.
    crashes: TestcaseDir,
}

impl State {
    /// Initialize the run-time environment in the given output directory.
    ///
    /// This involves creating the output directory and all required
    /// subdirectories.
    fn initialize<P: AsRef<Path>>(output_dir: P) -> Result<Self> {
        let symcc_dir = output_dir.as_ref();

        fs::create_dir(&symcc_dir).with_context(|| {
            format!("Failed to create SymCC's directory {}", symcc_dir.display())
        })?;
        let symcc_queue = TestcaseDir {
            path: symcc_dir.join("queue"),
            current_id: 0,
        };
        fs::create_dir(&symcc_queue.path).with_context(|| {
            format!(
                "Failed to create SymCC's queue {}",
                symcc_queue.path.display()
            )
        })?;
        let symcc_hangs = TestcaseDir {
            path: symcc_dir.join("hangs"),
            current_id: 0,
        };
        fs::create_dir(&symcc_hangs.path)
            .with_context(|| format!("Failed to create {}", symcc_hangs.path.display()))?;
        let symcc_crashes = TestcaseDir {
            path: symcc_dir.join("crashes"),
            current_id: 0,
        };
        fs::create_dir(&symcc_crashes.path)
            .with_context(|| format!("Failed to create {}", symcc_crashes.path.display()))?;

        Ok(State {
            current_bitmap: [0u8; 65536],
            processed_files: HashSet::new(),
            queue: symcc_queue,
            hangs: symcc_hangs,
            crashes: symcc_crashes,
        })
    }
}

/// The run-time configuration of SymCC.
struct SymCC {
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
    fn new(output_dir: PathBuf, command: &Vec<String>) -> Self {
        let input_file = output_dir.join(".cur_input");

        SymCC {
            use_standard_input: command.contains(&String::from("@@")),
            bitmap: output_dir.join("bitmap"),
            command: insert_input_file(command, &input_file),
            input_file: input_file,
        }
    }

    /// Run SymCC on the given input, writing results to the provided temporary
    /// directory.
    fn run<P: AsRef<Path>, Q: AsRef<Path>>(&self, input: P, tmp_dir: Q) -> Result<PathBuf> {
        fs::copy(&input, &self.input_file).with_context(|| {
            format!(
                "Failed to copy the test case {} to our workbench at {}",
                input.as_ref().display(),
                self.input_file.display()
            )
        })?;

        let output_dir = tmp_dir.as_ref().join("output");
        fs::create_dir(&output_dir).with_context(|| {
            format!(
                "Failed to create the output directory {} for SymCC",
                output_dir.display()
            )
        })?;

        let log_file_path = tmp_dir.as_ref().join("symcc.log");
        let log_file = File::create(&log_file_path).with_context(|| {
            format!(
                "Failed to create SymCC's log file at {}",
                log_file_path.display()
            )
        })?;

        let mut analysis_command = Command::new("timeout");
        analysis_command
            .args(&["-k", "5", &TIMEOUT.to_string()])
            .args(&self.command)
            .env("SYMCC_ENABLE_LINEARIZATION", "1")
            .env("SYMCC_AFL_COVERAGE_MAP", &self.bitmap)
            .env("SYMCC_OUTPUT_DIR", &output_dir)
            .stdout(
                log_file
                    .try_clone()
                    .context("Failed to open SymCC's log file a second time")?,
            )
            .stderr(log_file);

        if self.use_standard_input {
            analysis_command.stdin(Stdio::piped());
        } else {
            analysis_command.stdin(Stdio::null());
            analysis_command.env("SYMCC_INPUT_FILE", &self.input_file);
        }

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
        if let Some(code) = result.code() {
            println!("SymCC returned code {}", code);
        }

        Ok(output_dir)
    }
}

/// The possible outcomes of test-case evaluation.
#[derive(Debug, PartialEq, Eq)]
enum TestcaseResult {
    Uninteresting,
    New,
    Hang,
    Crash,
}

/// Check if the given test case provides new coverage, crashes, or times out;
/// copy it to the corresponding location.
fn process_new_testcase<P: AsRef<Path>, Q: AsRef<Path>, R: AsRef<Path>>(
    testcase: P,
    parent: Q,
    tmp_dir: R,
    afl_config: &AflConfig,
    state: &mut State,
) -> Result<TestcaseResult> {
    let testcase_bitmap = tmp_dir.as_ref().join("testcase_bitmap");
    let mut afl_show_map_child = Command::new(&afl_config.show_map)
        .args(&["-t", "5000", "-m", "none", "-b", "-o"])
        .arg(&testcase_bitmap)
        .args(insert_input_file(&afl_config.target_command, &testcase))
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .stdin(if afl_config.use_standard_input {
            Stdio::piped()
        } else {
            Stdio::null()
        })
        .spawn()
        .context("Failed to run afl-showmap")?;

    if afl_config.use_standard_input {
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
    match afl_show_map_status
        .code()
        .expect("No exit code available for afl-showmap")
    {
        0 => {
            // Successfully generated the map.
            let testcase_bitmap = fs::read(&testcase_bitmap).with_context(|| {
                format!(
                    "Failed to read the AFL bitmap that \
                     afl-showmap should have generated at {}",
                    testcase_bitmap.display()
                )
            })?;
            ensure!(
                testcase_bitmap.len() == state.current_bitmap.len(),
                "The map generated by afl-showmap has the wrong size ({})",
                testcase_bitmap.len()
            );

            let mut interesting = false;
            for (known, new) in state.current_bitmap.iter_mut().zip(testcase_bitmap) {
                if *known != (*known | new) {
                    *known |= new;
                    interesting = true;
                }
            }

            if interesting {
                copy_testcase(&testcase, &mut state.queue, parent).with_context(|| {
                    format!(
                        "Failed to enqueue the new test case {}",
                        testcase.as_ref().display()
                    )
                })?;

                Ok(TestcaseResult::New)
            } else {
                Ok(TestcaseResult::Uninteresting)
            }
        }
        1 => {
            // The target timed out or failed to execute.
            copy_testcase(testcase, &mut state.hangs, parent)?;
            Ok(TestcaseResult::Hang)
        }
        2 => {
            // The target crashed.
            copy_testcase(testcase, &mut state.crashes, parent)?;
            Ok(TestcaseResult::Crash)
        }
        unexpected => panic!("Unexpected return code {} from afl-showmap", unexpected),
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
