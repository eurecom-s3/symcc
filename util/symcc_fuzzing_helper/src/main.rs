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

mod symcc;

use anyhow::{Context, Result};
use std::collections::HashSet;
use std::fs;
use std::fs::File;
use std::io::Write;
use std::path::{Path, PathBuf};
use std::thread;
use std::time::{Duration, Instant};
use std::ffi::OsString;
use structopt::StructOpt;
use symcc::{AflConfig, AflMap, AflShowmapResult, SymCC, TestcaseDir};
use tempfile::tempdir;

const STATS_INTERVAL_SEC: u64 = 60;

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

    /// Wait until sync target shows up
    #[structopt(short = "W")]
    wait_for_sync: bool,

    /// force -Q qemu_mode
    #[structopt(short = "Q")]
    force_qemu_mode: bool,

    /// Minimum ID in the sync fuzzer queue (restart option workaround)
    #[structopt(short = "S", default_value("0"), required(false))]
    start_id: u32,

    /// Program under test
    command: Vec<String>,
}

/// Execution statistics.
#[derive(Debug, Default)]
struct Stats {
    /// Number of successful executions.
    total_count: u32,

    /// Time spent in successful executions of SymCC.
    total_time: Duration,

    /// Time spent in the solver as part of successfully running SymCC.
    solver_time: Option<Duration>,

    /// Number of failed executions.
    failed_count: u32,

    /// Time spent in failed SymCC executions.
    failed_time: Duration,
}

impl Stats {
    fn add_execution(&mut self, result: &symcc::SymCCResult) {
        if result.killed {
            self.failed_count += 1;
            self.failed_time += result.time;
        } else {
            self.total_count += 1;
            self.total_time += result.time;
            self.solver_time = match (self.solver_time, result.solver_time) {
                (None, None) => None,
                (Some(t), None) => Some(t), // no queries in this execution
                (None, Some(t)) => Some(t),
                (Some(a), Some(b)) => Some(a + b),
            };
        }
    }

    fn log(&self, out: &mut impl Write) -> Result<()> {
        writeln!(out, "Successful executions: {}", self.total_count)?;
        writeln!(
            out,
            "Time in successful executions: {}ms",
            self.total_time.as_millis()
        )?;

        if self.total_count > 0 {
            writeln!(
                out,
                "Avg time per successful execution: {}ms",
                (self.total_time / self.total_count).as_millis()
            )?;
        }

        if let Some(st) = self.solver_time {
            writeln!(
                out,
                "Solver time (successful executions): {}ms",
                st.as_millis()
            )?;

            if self.total_time.as_secs() > 0 {
                let solver_share = st.as_millis() as f64 / self.total_time.as_millis() as f64 *
                    100_f64;
                writeln!(
                    out,
                    "Solver time share (successful executions): {:.2}% (-> {:.2}% in execution)",
                    solver_share,
                    100_f64 - solver_share
                )?;
                writeln!(
                    out,
                    "Avg solver time per successful execution: {}ms",
                    (st / self.total_count).as_millis()
                )?;
            }
        }

        writeln!(out, "Failed executions: {}", self.failed_count)?;
        writeln!(
            out,
            "Time spent on failed executions: {}ms",
            self.failed_time.as_millis()
        )?;

        if self.failed_count > 0 {
            writeln!(
                out,
                "Avg time in failed executions: {}ms",
                (self.failed_time / self.failed_count).as_millis()
            )?;
        }

        writeln!(
            out,
            "--------------------------------------------------------------------------------"
        )?;

        Ok(())
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

    /// Run-time statistics.
    stats: Stats,

    /// When did we last output the statistics?
    last_stats_output: Instant,

    /// Write statistics to this file.
    stats_file: File,
}

impl State {
    /// Initialize the run-time environment in the given output directory.
    ///
    /// This involves creating the output directory and all required
    /// subdirectories.
    fn initialize(output_dir: impl AsRef<Path>) -> Result<Self> {
        let symcc_dir = output_dir.as_ref();

        fs::create_dir(&symcc_dir).with_context(|| {
            format!("Failed to create SymCC's directory {}", symcc_dir.display())
        })?;
        let symcc_queue = TestcaseDir::new(symcc_dir.join("queue")).context(
            "Failed to create SymCC's queue",
        )?;
        let symcc_hangs = TestcaseDir::new(symcc_dir.join("hangs"))?;
        let symcc_crashes = TestcaseDir::new(symcc_dir.join("crashes"))?;
        let stats_file = File::create(symcc_dir.join("stats"))?;

        Ok(State {
            current_bitmap: AflMap::new(),
            processed_files: HashSet::new(),
            queue: symcc_queue,
            hangs: symcc_hangs,
            crashes: symcc_crashes,
            stats: Default::default(), // Is this bad style?
            last_stats_output: Instant::now(),
            stats_file,
        })
    }

    /// Run a single input through SymCC and process the new test cases it
    /// generates.
    fn test_input(
        &mut self,
        input: impl AsRef<Path>,
        symcc: &SymCC,
        afl_config: &AflConfig,
    ) -> Result<()> {
        log::info!("Running on input {}", input.as_ref().display());

        let tmp_dir = tempdir().context(
            "Failed to create a temporary directory for this execution of SymCC",
        )?;

        let mut num_interesting = 0u64;
        let mut num_total = 0u64;

        let symcc_result = symcc.run(&input, tmp_dir.path().join("output")).context(
            "Failed to run SymCC",
        )?;
        for new_test in symcc_result.test_cases.iter() {

            if afl_config.use_qemu_mode {
                let res = process_new_testcase(&new_test, &input, &tmp_dir, &afl_config, self);

                let res = match res {
                    Ok(result) => result,
                    Err(_) => continue,
                };

                num_total += 1;
                if res == TestcaseResult::New {
                    log::debug!("Test case is interesting");
                    num_interesting += 1;
                }
            }
        }

        log::info!(
            "Generated {} test cases ({} new)",
            num_total,
            num_interesting
        );

        if symcc_result.killed {
            log::info!(
                "The target process was killed (probably timeout or out of memory); \
                 archiving to {}",
                self.hangs.path.display()
            );
            symcc::copy_testcase(&input, &mut self.hangs, &input)
                .context("Failed to archive the test case")?;
        }

        self.processed_files.insert(input.as_ref().to_path_buf());
        self.stats.add_execution(&symcc_result);
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

    let mut errors = 1;
    let afl_queue = options.output_dir.join(&options.fuzzer_name).join("queue");
    let symcc_dir = options.output_dir.join(&options.name);
    let mut afl_config = AflConfig {
        use_standard_input: true,
        use_qemu_mode: false,
        target_command: vec![],
        show_map: PathBuf::new(),
        queue: PathBuf::new(),
    };

    while errors != 0 {

        errors = 0;

        if !options.output_dir.is_dir() {
            log::error!(
                "The directory {} does not exist!",
                options.output_dir.display()
            );
            errors += 1;
            //return Ok(());
        }

        if !afl_queue.is_dir() {
            log::error!("The AFL queue {} does not exist!", afl_queue.display());
            errors += 1;
            //return Ok(());
        }

        match AflConfig::load(options.output_dir.join(&options.fuzzer_name)) {
            Ok(result) => afl_config = result,
            Err(_) => {
                log::error!("Could not load afl-fuzz fuzzer_stats");
                errors += 1
            }
        }

        if errors > 0 {
            if !options.wait_for_sync {
                return Ok(());
            }
            log::error!("waiting for afl-fuzz to start for 5 seconds ...");
            thread::sleep(Duration::from_secs(5));
        }
    }

    if symcc_dir.is_dir() {
        log::error!("{} already exists; removing ...", symcc_dir.display());
        fs::remove_dir_all(&symcc_dir)?;
    }

    if options.force_qemu_mode && !afl_config.use_qemu_mode {
        // We need to overwrite the binary afl-showmap is running as the one
        // in the afl config is an instrumented one
        if options.command[0].contains("symqemu") {
            afl_config.target_command[1] = OsString::from(options.command[1].clone())
                .to_os_string();
        }
        afl_config.use_qemu_mode = true;
    }
    log::debug!("AFL configuration: {:?}", &afl_config);

    let symcc = SymCC::new(
        symcc_dir.clone(),
        &options.command,
        afl_config.use_qemu_mode,
    );
    log::debug!("SymCC configuration: {:?}", &symcc);

    let mut state = State::initialize(symcc_dir)?;

    loop {
        match afl_config
            .best_new_testcase(&state.processed_files)
            .context("Failed to check for new test cases")? {
            None => {
                log::debug!("Waiting for new test cases...");
                thread::sleep(Duration::from_secs(5));
            }
            Some(input) => {
                let filename = input.file_name().unwrap().to_string_lossy();
                if filename.starts_with("id:") {
                    let id_str = filename.get(3..9).unwrap();
                    let id = id_str.parse().unwrap_or(0);
                    if id >= options.start_id {
                        state.test_input(&input, &symcc, &afl_config)?
                    } else {
                        log::info!("Skipping input {} because it has a low ID", input.display());
                        state.processed_files.insert(input.to_path_buf());
                    }
                } else {
                    log::info!("Skipping invalid input name {}", input.display());
                    state.processed_files.insert(input.to_path_buf());
                }
            }

        }

        if state.last_stats_output.elapsed().as_secs() > STATS_INTERVAL_SEC {
            if let Err(e) = state.stats.log(&mut state.stats_file) {
                log::error!("Failed to log run-time statistics: {}", e);
            }
            state.last_stats_output = Instant::now();
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
fn process_new_testcase(
    testcase: impl AsRef<Path>,
    parent: impl AsRef<Path>,
    tmp_dir: impl AsRef<Path>,
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
        })
        .unwrap_or(AflShowmapResult::Ignore) {
        AflShowmapResult::Ignore => {
            log::info!("afl-showmap error'ed, ignoring");
            Ok(TestcaseResult::Uninteresting)
        }
        AflShowmapResult::Success(testcase_bitmap) => {
            let interesting = state.current_bitmap.merge(&testcase_bitmap);
            if interesting {
                symcc::copy_testcase(&testcase, &mut state.queue, parent)
                    .with_context(|| {
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
            symcc::copy_testcase(&testcase, &mut state.queue, &parent)
                .with_context(|| {
                    format!(
                        "Failed to enqueue the new test case {}",
                        testcase.as_ref().display()
                    )
                })?;
            Ok(TestcaseResult::Crash)
        }
    }
}
