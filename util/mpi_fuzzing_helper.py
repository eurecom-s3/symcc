#!/usr/bin/env python3
"""
MPI-parallel fuzzing helper for SymCC + AFL integration.

This is the MPI-parallel equivalent of symcc_fuzzing_helper. It monitors
an AFL fuzzer's queue and distributes SymCC executions across MPI workers.
New test cases that produce novel coverage are fed back to AFL.

Architecture:
    Rank 0 (Master): Monitors AFL queue, distributes inputs, triages results
    Ranks 1..N-1 (Workers): Run SymCC on assigned inputs

Usage:
    mpirun -np <N> python3 mpi_fuzzing_helper.py \
        -a <fuzzer_name> -o <afl_output_dir> -n <symcc_name> -- TARGET [ARGS...]

Requirements:
    - mpi4py  (pip install mpi4py)
    - An MPI implementation (OpenMPI, MPICH, etc.)
    - AFL (afl-showmap must be available)
    - SymCC-instrumented target binary

Example:
    # Start AFL first:
    afl-fuzz -M fuzzer01 -i seeds -o /tmp/afl_out -- ./target_afl @@

    # Then start SymCC MPI helper:
    mpirun -np 8 python3 mpi_fuzzing_helper.py \
        -a fuzzer01 -o /tmp/afl_out -n symcc -- ./target_symcc @@
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from mpi4py import MPI

# MPI tags
TAG_WORK = 1
TAG_RESULT = 2
TAG_STOP = 3
TAG_READY = 4

TIMEOUT_SEC = 90
SHOWMAP_TIMEOUT_MS = "5000"
STATS_INTERVAL_SEC = 60


def file_hash(path):
    """Return SHA-256 hash of file contents."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()


class AflConfig:
    """AFL fuzzer configuration, read from fuzzer_stats."""

    def __init__(self, fuzzer_output_dir):
        self.queue = os.path.join(fuzzer_output_dir, "queue")
        stats_path = os.path.join(fuzzer_output_dir, "fuzzer_stats")

        with open(stats_path) as f:
            stats = f.read()

        # Parse the command line from fuzzer_stats
        for line in stats.splitlines():
            if line.startswith("command_line"):
                cmd_str = line.split(":", 1)[1].strip()
                parts = cmd_str.split()
                break
        else:
            raise RuntimeError("Could not find command_line in fuzzer_stats")

        # Find afl-showmap path (same dir as afl-fuzz)
        afl_binary = parts[0]
        afl_dir = os.path.dirname(afl_binary) or "."
        self.show_map = os.path.join(afl_dir, "afl-showmap")

        # Extract target command (after --)
        try:
            dash_idx = parts.index("--")
            self.target_command = parts[dash_idx:]  # includes '--'
        except ValueError:
            self.target_command = parts[-1:]

        self.use_stdin = "@@" not in self.target_command
        self.use_qemu = "-Q" in parts

    def best_new_testcases(self, seen, batch_size=None):
        """
        Return a list of unseen test cases from the AFL queue,
        sorted by priority (new coverage first, then seed-derived, then by size).
        """
        candidates = []
        if not os.path.isdir(self.queue):
            return candidates

        for fname in os.listdir(self.queue):
            fpath = os.path.join(self.queue, fname)
            if not os.path.isfile(fpath):
                continue
            if fpath in seen:
                continue

            # Score: (new_coverage, derived_from_seed, -file_size)
            has_cov = fname.endswith("+cov")
            from_seed = "orig:" in fname
            try:
                size = os.path.getsize(fpath)
            except OSError:
                size = 0
            candidates.append((has_cov, from_seed, -size, fpath))

        # Sort descending by score
        candidates.sort(reverse=True)
        paths = [c[3] for c in candidates]

        if batch_size is not None:
            return paths[:batch_size]
        return paths

    def run_showmap(self, testcase, bitmap_path):
        """
        Run afl-showmap on a test case.

        Returns:
            ("success", bitmap_data) | ("hang", None) | ("crash", None)
        """
        cmd = [self.show_map]
        if self.use_qemu:
            cmd.append("-Q")
        cmd.extend(["-t", SHOWMAP_TIMEOUT_MS, "-m", "none", "-b", "-o", bitmap_path])

        # Build target command with @@ replaced
        for arg in self.target_command:
            if arg == "@@":
                cmd.append(str(testcase))
            else:
                cmd.append(arg)

        try:
            if self.use_stdin:
                with open(testcase, "rb") as inf:
                    proc = subprocess.run(
                        cmd, stdin=inf, stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL
                    )
            else:
                proc = subprocess.run(
                    cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL
                )

            if proc.returncode == 0:
                with open(bitmap_path, "rb") as f:
                    bitmap_data = f.read()
                return "success", bitmap_data
            elif proc.returncode == 1:
                return "hang", None
            elif proc.returncode == 2:
                return "crash", None
            else:
                return "error", None
        except Exception as e:
            print(f"[Master] afl-showmap error: {e}", file=sys.stderr)
            return "error", None


class CoverageBitmap:
    """Cumulative coverage bitmap."""

    def __init__(self):
        self.data = None

    def merge(self, new_data):
        """Merge new bitmap data. Returns True if new coverage found."""
        if self.data is None:
            self.data = bytearray(new_data)
            return True

        if len(self.data) != len(new_data):
            return False

        interesting = False
        for i in range(len(self.data)):
            old = self.data[i]
            merged = old | new_data[i]
            if merged != old:
                self.data[i] = merged
                interesting = True
        return interesting


class Stats:
    """Execution statistics."""

    def __init__(self):
        self.total_count = 0
        self.total_time = 0.0
        self.failed_count = 0
        self.failed_time = 0.0
        self.generated_count = 0
        self.interesting_count = 0

    def add_execution(self, elapsed, killed):
        if killed:
            self.failed_count += 1
            self.failed_time += elapsed
        else:
            self.total_count += 1
            self.total_time += elapsed

    def log(self, f):
        f.write(f"Successful executions: {self.total_count}\n")
        f.write(f"Time in successful executions: {self.total_time*1000:.0f}ms\n")
        if self.total_count > 0:
            avg = self.total_time / self.total_count * 1000
            f.write(f"Avg time per successful execution: {avg:.0f}ms\n")
        f.write(f"Failed executions: {self.failed_count}\n")
        f.write(f"Time in failed executions: {self.failed_time*1000:.0f}ms\n")
        if self.failed_count > 0:
            avg = self.failed_time / self.failed_count * 1000
            f.write(f"Avg time per failed execution: {avg:.0f}ms\n")
        f.write(f"Total test cases generated: {self.generated_count}\n")
        f.write(f"Interesting test cases: {self.interesting_count}\n")
        f.write("-" * 80 + "\n")
        f.flush()


def run_symcc_worker(target_cmd, input_file, output_dir, timeout_sec, use_stdin):
    """Run SymCC on a single input. Returns (new_tests_data, retcode, elapsed)."""
    os.makedirs(output_dir, exist_ok=True)

    env = os.environ.copy()
    env["SYMCC_OUTPUT_DIR"] = output_dir
    env["SYMCC_ENABLE_LINEARIZATION"] = "1"

    if use_stdin:
        cmd = ["timeout", "-k", "5", str(timeout_sec)] + target_cmd
    else:
        env["SYMCC_INPUT_FILE"] = str(input_file)
        cmd = ["timeout", "-k", "5", str(timeout_sec)] + [
            arg.replace("@@", str(input_file)) for arg in target_cmd
        ]

    start = time.monotonic()
    try:
        if use_stdin:
            with open(input_file, "rb") as inf:
                proc = subprocess.run(
                    cmd, stdin=inf, stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL, env=env
                )
        else:
            proc = subprocess.run(
                cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL, env=env
            )
        retcode = proc.returncode
    except Exception as e:
        print(f"[Worker {MPI.COMM_WORLD.Get_rank()}] Error: {e}", file=sys.stderr)
        retcode = -1

    elapsed = time.monotonic() - start
    killed = retcode in (124, -9, 137)  # timeout codes

    # Collect test cases
    new_tests = []
    if os.path.isdir(output_dir):
        for fname in os.listdir(output_dir):
            fpath = os.path.join(output_dir, fname)
            if os.path.isfile(fpath):
                try:
                    with open(fpath, "rb") as f:
                        content = f.read()
                    new_tests.append({"name": fname, "content": content})
                except (IOError, OSError):
                    pass

    return new_tests, retcode, elapsed, killed


def master(comm, args):
    """Master process: monitors AFL queue, distributes work, triages results."""
    size = comm.Get_size()
    num_workers = size - 1

    if num_workers == 0:
        print("Error: need at least 2 MPI processes.", file=sys.stderr)
        return

    # Setup
    afl_queue_dir = os.path.join(args.output_dir, args.fuzzer_name)
    symcc_dir = os.path.join(args.output_dir, args.name)

    if os.path.exists(symcc_dir):
        print(f"Error: {symcc_dir} already exists. "
              f"We don't support resuming.", file=sys.stderr)
        for rank in range(1, size):
            while comm.iprobe(source=rank, tag=TAG_READY):
                comm.recv(source=rank, tag=TAG_READY)
            comm.send(None, dest=rank, tag=TAG_STOP)
        return

    os.makedirs(symcc_dir)
    queue_dir = os.path.join(symcc_dir, "queue")
    hangs_dir = os.path.join(symcc_dir, "hangs")
    crashes_dir = os.path.join(symcc_dir, "crashes")
    os.makedirs(queue_dir)
    os.makedirs(hangs_dir)
    os.makedirs(crashes_dir)

    stats_file = open(os.path.join(symcc_dir, "stats"), "w")
    bitmap_path_triage = os.path.join(symcc_dir, ".triage_bitmap")

    # Load AFL config
    try:
        afl_config = AflConfig(afl_queue_dir)
    except Exception as e:
        print(f"Error loading AFL config: {e}", file=sys.stderr)
        for rank in range(1, size):
            while comm.iprobe(source=rank, tag=TAG_READY):
                comm.recv(source=rank, tag=TAG_READY)
            comm.send(None, dest=rank, tag=TAG_STOP)
        return

    print(f"[Master] SymCC MPI Fuzzing Helper")
    print(f"[Master] Workers: {num_workers}")
    print(f"[Master] AFL queue: {afl_config.queue}")
    print(f"[Master] SymCC output: {symcc_dir}")

    coverage = CoverageBitmap()
    stats = Stats()
    processed_files = set()
    active_workers = {}  # rank -> input_path
    queue_id = 0
    last_stats_time = time.monotonic()

    while True:
        # Get new test cases from AFL queue
        new_inputs = afl_config.best_new_testcases(processed_files, batch_size=num_workers * 2)

        # Distribute work to ready workers
        input_idx = 0
        while input_idx < len(new_inputs) and comm.iprobe(source=MPI.ANY_SOURCE, tag=TAG_READY):
            status = MPI.Status()
            comm.recv(source=MPI.ANY_SOURCE, tag=TAG_READY, status=status)
            worker_rank = status.Get_source()

            input_file = new_inputs[input_idx]
            input_idx += 1

            # Send input content to worker
            try:
                with open(input_file, "rb") as f:
                    content = f.read()
                comm.send({"path": input_file, "content": content},
                          dest=worker_rank, tag=TAG_WORK)
                active_workers[worker_rank] = input_file
                processed_files.add(input_file)
            except (IOError, OSError) as e:
                print(f"[Master] Error reading {input_file}: {e}", file=sys.stderr)
                continue

        # Collect results from workers
        while comm.iprobe(source=MPI.ANY_SOURCE, tag=TAG_RESULT):
            status = MPI.Status()
            result = comm.recv(source=MPI.ANY_SOURCE, tag=TAG_RESULT, status=status)
            worker_rank = status.Get_source()

            input_path = active_workers.pop(worker_rank, "unknown")
            new_tests = result.get("new_tests", [])
            retcode = result.get("retcode", 0)
            elapsed = result.get("elapsed", 0)
            killed = result.get("killed", False)

            stats.add_execution(elapsed, killed)

            num_interesting = 0
            for tc in new_tests:
                stats.generated_count += 1
                tc_content = tc["content"]

                # Write to temp file for triage
                with tempfile.NamedTemporaryFile(delete=False, dir=symcc_dir,
                                                  prefix=".tc_") as tmp:
                    tmp.write(tc_content)
                    tmp_path = tmp.name

                try:
                    # Check coverage via afl-showmap
                    result_type, bitmap_data = afl_config.run_showmap(
                        tmp_path, bitmap_path_triage
                    )

                    if result_type == "success" and bitmap_data:
                        is_new = coverage.merge(bitmap_data)
                        if is_new:
                            # Save to SymCC queue (AFL will pick it up)
                            orig_name = os.path.basename(input_path)
                            # Extract source id
                            src_id = "000000"
                            if orig_name.startswith("id:") and len(orig_name) >= 9:
                                src_id = orig_name[3:9]
                            new_name = f"id:{queue_id:06d},src:{src_id}"
                            dest = os.path.join(queue_dir, new_name)
                            shutil.copy2(tmp_path, dest)
                            queue_id += 1
                            num_interesting += 1
                            stats.interesting_count += 1

                    elif result_type == "crash":
                        # Save crashing input
                        orig_name = os.path.basename(input_path)
                        src_id = "000000"
                        if orig_name.startswith("id:") and len(orig_name) >= 9:
                            src_id = orig_name[3:9]
                        crash_name = f"id:{queue_id:06d},src:{src_id}"
                        shutil.copy2(tmp_path, os.path.join(crashes_dir, crash_name))
                        # Also add to queue
                        shutil.copy2(tmp_path, os.path.join(queue_dir, crash_name))
                        queue_id += 1
                        num_interesting += 1
                        stats.interesting_count += 1

                finally:
                    try:
                        os.unlink(tmp_path)
                    except OSError:
                        pass

            if killed:
                # Save hanging input
                orig_name = os.path.basename(input_path)
                src_id = "000000"
                if orig_name.startswith("id:") and len(orig_name) >= 9:
                    src_id = orig_name[3:9]
                hang_name = f"id:{queue_id:06d},src:{src_id}"
                # Write from the original path since we already processed it
                # (the worker sent back the input file was already at input_path)

            input_short = os.path.basename(input_path)[:30]
            print(f"[Master] Worker {worker_rank}: {input_short} -> "
                  f"{len(new_tests)} generated, {num_interesting} interesting "
                  f"({elapsed:.1f}s, ret={retcode})")

        # Periodic stats output
        if time.monotonic() - last_stats_time > STATS_INTERVAL_SEC:
            stats.log(stats_file)
            last_stats_time = time.monotonic()
            print(f"[Master] Stats: {stats.total_count} ok, "
                  f"{stats.failed_count} failed, "
                  f"{stats.interesting_count} interesting / "
                  f"{stats.generated_count} total")

        # If no new inputs and no active workers, wait
        if not new_inputs and not active_workers:
            time.sleep(5)
        else:
            time.sleep(0.05)


def worker(comm, args):
    """Worker process: receives inputs, runs SymCC, sends back results."""
    rank = comm.Get_rank()
    target_cmd = args.target
    use_stdin = "@@" not in target_cmd

    worker_dir = tempfile.mkdtemp(prefix=f"symcc_mpi_w{rank}_")

    # Create a local bitmap file for the SYMCC_AFL_COVERAGE_MAP
    bitmap_file = os.path.join(worker_dir, "bitmap")

    while True:
        # Signal ready
        comm.send(rank, dest=0, tag=TAG_READY)

        # Wait for work or stop
        status = MPI.Status()
        msg = comm.recv(source=0, tag=MPI.ANY_TAG, status=status)

        if status.Get_tag() == TAG_STOP:
            break

        if status.Get_tag() != TAG_WORK:
            continue

        input_path = msg["path"]
        input_content = msg["content"]

        # Write input to local file
        local_input = os.path.join(worker_dir, "current_input")
        with open(local_input, "wb") as f:
            f.write(input_content)

        # Run SymCC
        run_output = os.path.join(worker_dir, f"output_{time.monotonic_ns()}")

        # Set up environment for AFL coverage map
        env_backup = os.environ.get("SYMCC_AFL_COVERAGE_MAP")
        os.environ["SYMCC_AFL_COVERAGE_MAP"] = bitmap_file

        try:
            new_tests, retcode, elapsed, killed = run_symcc_worker(
                target_cmd, local_input, run_output, TIMEOUT_SEC, use_stdin
            )

            result = {
                "new_tests": new_tests,
                "retcode": retcode,
                "elapsed": elapsed,
                "killed": killed,
            }
        except Exception as e:
            print(f"[Worker {rank}] Error: {e}", file=sys.stderr)
            result = {
                "new_tests": [],
                "retcode": -1,
                "elapsed": 0,
                "killed": False,
            }

        # Restore env
        if env_backup is not None:
            os.environ["SYMCC_AFL_COVERAGE_MAP"] = env_backup
        elif "SYMCC_AFL_COVERAGE_MAP" in os.environ:
            del os.environ["SYMCC_AFL_COVERAGE_MAP"]

        # Clean up output
        shutil.rmtree(run_output, ignore_errors=True)

        # Send result
        comm.send(result, dest=0, tag=TAG_RESULT)

    shutil.rmtree(worker_dir, ignore_errors=True)


def parse_args():
    parser = argparse.ArgumentParser(
        description="MPI-parallel SymCC + AFL fuzzing helper",
        usage="mpirun -np <N> python3 %(prog)s -a FUZZER -o DIR -n NAME -- TARGET [ARGS...]",
    )
    parser.add_argument("-a", "--fuzzer-name", required=True,
                        help="AFL fuzzer instance name")
    parser.add_argument("-o", "--output-dir", required=True,
                        help="AFL output directory")
    parser.add_argument("-n", "--name", required=True,
                        help="Name for this SymCC instance")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Verbose output")
    parser.add_argument("target", nargs=argparse.REMAINDER,
                        help="Target command (after '--')")

    args = parser.parse_args()

    if args.target and args.target[0] == "--":
        args.target = args.target[1:]

    if not args.target:
        parser.error("No target command. Use: -- TARGET [ARGS...]")

    return args


def main():
    comm = MPI.COMM_WORLD
    rank = comm.Get_rank()

    args = parse_args()

    if rank == 0:
        master(comm, args)
    else:
        worker(comm, args)

    MPI.Finalize()


if __name__ == "__main__":
    main()
