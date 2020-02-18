mod symcc;

use anyhow::{Context, Result};
use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};
use std::thread;
use std::time::Duration;
use structopt::StructOpt;
use symcc::{AflConfig, AflMap, AflShowmapResult, SymCC, TestcaseDir};
use tempfile::tempdir;

// TODO extend timeout when idle? Possibly reprocess previously timed-out
// inputs.

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

    /// Enable verbose logging
    #[structopt(short = "v")]
    verbose: bool,

    /// Program under test
    command: Vec<String>,
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
        let symcc_queue =
            TestcaseDir::new(symcc_dir.join("queue")).context("Failed to create SymCC's queue")?;
        let symcc_hangs = TestcaseDir::new(symcc_dir.join("hangs"))?;
        let symcc_crashes = TestcaseDir::new(symcc_dir.join("crashes"))?;

        Ok(State {
            current_bitmap: AflMap::new(),
            processed_files: HashSet::new(),
            queue: symcc_queue,
            hangs: symcc_hangs,
            crashes: symcc_crashes,
        })
    }

    /// Run a single input through SymCC and process the new test cases it
    /// generates.
    fn test_input<P: AsRef<Path>>(
        &mut self,
        input: P,
        symcc: &SymCC,
        afl_config: &AflConfig,
    ) -> Result<()> {
        log::info!("Running on input {}", input.as_ref().display());

        let tmp_dir = tempdir()
            .context("Failed to create a temporary directory for this execution of SymCC")?;

        let mut num_interesting = 0u64;
        let mut num_total = 0u64;

        let (new_tests, killed) = symcc
            .run(&input, tmp_dir.path().join("output"))
            .context("Failed to run SymCC")?;
        for new_test in new_tests {
            let res = process_new_testcase(&new_test, &input, &tmp_dir, &afl_config, self)?;

            num_total += 1;
            if res == TestcaseResult::New {
                log::debug!("Test case is interesting");
                num_interesting += 1;
            }
        }

        log::info!(
            "Generated {} test cases ({} new)",
            num_total,
            num_interesting
        );

        if killed {
            log::info!(
                "The target process was killed (probably timeout or out of memory); \
                 archiving to {}",
                self.hangs.path.display()
            );
            symcc::copy_testcase(&input, &mut self.hangs, &input)
                .context("Failed to archive the test case")?;
        }

        self.processed_files.insert(input.as_ref().to_path_buf());
        Ok(())
    }
}

fn main() -> Result<()> {
    let options = CLI::from_args();
    env_logger::builder()
        .filter_level(if options.verbose {
            log::LevelFilter::Debug
        } else {
            log::LevelFilter::Info
        })
        .init();

    if !options.output_dir.is_dir() {
        log::error!(
            "The directory {} does not exist!",
            options.output_dir.display()
        );
        return Ok(());
    }

    let afl_queue = options.output_dir.join(&options.fuzzer_name).join("queue");
    if !afl_queue.is_dir() {
        log::error!("The AFL queue {} does not exist!", afl_queue.display());
        return Ok(());
    }

    let symcc_dir = options.output_dir.join(&options.name);
    if symcc_dir.is_dir() {
        log::error!(
            "{} already exists; we do not currently support resuming",
            symcc_dir.display()
        );
        return Ok(());
    }

    let symcc = SymCC::new(symcc_dir.clone(), &options.command);
    log::debug!("SymCC configuration: {:?}", &symcc);
    let afl_config = AflConfig::load(options.output_dir.join(&options.fuzzer_name))?;
    log::debug!("AFL configuration: {:?}", &afl_config);
    let mut state = State::initialize(symcc_dir)?;

    loop {
        match afl_config
            .best_new_testcase(&state.processed_files)
            .context("Failed to check for new test cases")?
        {
            None => {
                log::info!("Waiting for new test cases...");
                thread::sleep(Duration::from_secs(5));
            }
            Some(input) => state.test_input(&input, &symcc, &afl_config)?,
        }
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
    log::debug!("Processing test case {}", testcase.as_ref().display());

    let testcase_bitmap_path = tmp_dir.as_ref().join("testcase_bitmap");
    match afl_config
        .run_showmap(&testcase_bitmap_path, &testcase)
        .with_context(|| {
            format!(
                "Failed to check whether test case {} is interesting",
                &testcase.as_ref().display()
            )
        })? {
        AflShowmapResult::Success(testcase_bitmap) => {
            let interesting = state.current_bitmap.merge(&testcase_bitmap);
            if interesting {
                symcc::copy_testcase(&testcase, &mut state.queue, parent).with_context(|| {
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
        AflShowmapResult::Hang => {
            log::info!(
                "Ignoring new test case {} because afl-showmap timed out on it",
                testcase.as_ref().display()
            );
            Ok(TestcaseResult::Hang)
        }
        AflShowmapResult::Crash => {
            log::info!(
                "Test case {} crashes afl-showmap; it is probably interesting",
                testcase.as_ref().display()
            );
            symcc::copy_testcase(&testcase, &mut state.crashes, &parent)?;
            symcc::copy_testcase(&testcase, &mut state.queue, &parent).with_context(|| {
                format!(
                    "Failed to enqueue the new test case {}",
                    testcase.as_ref().display()
                )
            })?;
            Ok(TestcaseResult::Crash)
        }
    }
}
