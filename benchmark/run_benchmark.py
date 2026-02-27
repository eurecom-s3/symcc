#!/usr/bin/env python3
"""
SymCC MPI Parallelization Benchmark Suite

Tests the MPI-parallel concolic execution at different parallelism levels
and generates a comparison report.

Usage:
    python3 run_benchmark.py [options]

Options:
    --symcc PATH     Path to SymCC compiler (default: symcc in PATH)
    --np-list LIST   Comma-separated list of process counts (default: 1,2,4,8)
    --targets LIST   Comma-separated target names (default: all)
    --rounds N       Rounds per configuration (default: 3)
    --timeout T      Per-execution timeout in seconds (default: 60)
    --output DIR     Output directory for results (default: benchmark_results)
    --skip-build     Skip compilation step (use existing binaries)

The script:
  1. Compiles target programs with SymCC (or gcc for simulation mode)
  2. Runs the serial baseline (pure_concolic_execution.sh)
  3. Runs MPI-parallel at each configured parallelism level
  4. Measures: wall-clock time, test cases generated, unique paths found
  5. Generates a comparison report
"""

import argparse
import csv
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections import defaultdict
from datetime import datetime
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent.resolve()
SYMCC_ROOT = SCRIPT_DIR.parent
TARGETS_DIR = SCRIPT_DIR / "targets"
SEEDS_DIR = SCRIPT_DIR / "seeds"
MPI_SCRIPT = SYMCC_ROOT / "util" / "mpi_concolic_execution.py"
SERIAL_SCRIPT = SYMCC_ROOT / "util" / "pure_concolic_execution.sh"

# Target configs: name -> (source, input_len, seed_prefix, uses_file_arg)
TARGETS = {
    "maze":          ("maze.c",          16, "maze_",    True),
    "parser":        ("parser.c",        32, "parser_",  True),
    "deep_branches": ("deep_branches.c",  8, "deep_",    True),
    "crypto_check":  ("crypto_check.c",  16, "crypto_",  True),
}

# Public benchmark targets (set up via setup_public_benchmarks.sh)
# Format: name -> (binary_path_relative_to_public_dir, seed_dir, args_template)
# These are populated at runtime via --public flag
PUBLIC_DIR = SCRIPT_DIR / "public"


def _resolve_path(p):
    """Resolve a path: try absolute, then relative to cwd, then relative to SCRIPT_DIR."""
    p = str(p)
    if os.path.isabs(p):
        return p
    # Try relative to cwd
    abs_cwd = os.path.abspath(p)
    if os.path.exists(abs_cwd):
        return abs_cwd
    # Try relative to benchmark/ (SCRIPT_DIR)
    abs_script = os.path.abspath(os.path.join(str(SCRIPT_DIR), p))
    if os.path.exists(abs_script):
        return abs_script
    # Try relative to project root (SYMCC_ROOT)
    abs_root = os.path.abspath(os.path.join(str(SYMCC_ROOT), p))
    if os.path.exists(abs_root):
        return abs_root
    # Fall back to cwd-relative (will fail later with a clear error)
    return abs_cwd


def run_cmd(cmd, timeout=300, capture=True):
    """Run a command and return (returncode, stdout, stderr, elapsed)."""
    start = time.monotonic()
    try:
        result = subprocess.run(
            cmd, capture_output=capture, text=True, timeout=timeout
        )
        elapsed = time.monotonic() - start
        if capture:
            return result.returncode, result.stdout, result.stderr, elapsed
        return result.returncode, "", "", elapsed
    except subprocess.TimeoutExpired:
        elapsed = time.monotonic() - start
        return -1, "", "TIMEOUT", elapsed


def find_symcc():
    """Find the SymCC compiler."""
    for candidate in ["symcc", str(SYMCC_ROOT / "build" / "symcc")]:
        if shutil.which(candidate):
            return candidate
    return None


def build_targets(compiler, output_dir):
    """Compile all target programs."""
    binaries = {}
    os.makedirs(output_dir, exist_ok=True)

    for name, (source, _, _, _) in TARGETS.items():
        src_path = TARGETS_DIR / source
        bin_path = Path(output_dir) / f"{name}_symcc"

        print(f"  Compiling {name}... ", end="", flush=True)
        ret, _, stderr, elapsed = run_cmd(
            [compiler, "-O2", str(src_path), "-o", str(bin_path)]
        )
        if ret == 0:
            print(f"OK ({elapsed:.1f}s)")
            binaries[name] = str(bin_path)
        else:
            print(f"FAILED (ret={ret})")
            if stderr:
                print(f"    {stderr[:200]}")
    return binaries


def build_targets_gcc(output_dir):
    """Compile with gcc as fallback (for simulation/framework testing)."""
    binaries = {}
    os.makedirs(output_dir, exist_ok=True)

    for name, (source, _, _, _) in TARGETS.items():
        src_path = TARGETS_DIR / source
        bin_path = Path(output_dir) / f"{name}_native"

        print(f"  Compiling {name} (gcc)... ", end="", flush=True)
        ret, _, stderr, elapsed = run_cmd(
            ["gcc", "-O2", str(src_path), "-o", str(bin_path)]
        )
        if ret == 0:
            print(f"OK ({elapsed:.1f}s)")
            binaries[name] = str(bin_path)
        else:
            print(f"FAILED")
    return binaries


def build_coverage_targets(output_dir):
    """Compile targets with gcc --coverage for coverage measurement."""
    cov_binaries = {}
    cov_dirs = {}
    os.makedirs(output_dir, exist_ok=True)

    for name, (source, _, _, _) in TARGETS.items():
        src_path = TARGETS_DIR / source
        cov_dir = os.path.join(output_dir, f"cov_{name}")
        os.makedirs(cov_dir, exist_ok=True)

        # Copy source to cov dir so gcno/gcda files are co-located
        cov_src = os.path.join(cov_dir, source)
        shutil.copy2(str(src_path), cov_src)

        # Binary name must match source basename for gcov to find .gcno/.gcda
        base_name = os.path.splitext(source)[0]
        bin_path = os.path.join(cov_dir, base_name)
        print(f"  Compiling {name} (coverage)... ", end="", flush=True)
        ret, _, stderr, elapsed = run_cmd(
            ["gcc", "--coverage", "-O0", "-g", cov_src, "-o", bin_path],
            timeout=60
        )
        if ret == 0:
            print(f"OK ({elapsed:.1f}s)")
            cov_binaries[name] = bin_path
            cov_dirs[name] = cov_dir
        else:
            print(f"FAILED")
            if stderr:
                print(f"    {stderr[:200]}")

    return cov_binaries, cov_dirs


def measure_coverage(cov_binary, cov_dir, source_file, test_case_dir,
                     uses_file=True, timeout_per_case=5):
    """
    Run all test cases through the coverage binary and measure coverage.

    Uses a shell loop to batch-execute test cases, avoiding per-file
    subprocess fork overhead (~100x faster for thousands of test cases).

    Returns dict with: line_cov, branch_cov, crashes, total_cases
    """
    # Clear old .gcda files
    for f in os.listdir(cov_dir):
        if f.endswith(".gcda"):
            os.remove(os.path.join(cov_dir, f))

    crashes = 0
    total_cases = 0

    if not os.path.isdir(test_case_dir):
        return {"line_cov": 0.0, "branch_cov": 0.0, "crashes": 0, "total_cases": 0}

    test_files = [os.path.join(test_case_dir, f)
                  for f in sorted(os.listdir(test_case_dir))
                  if os.path.isfile(os.path.join(test_case_dir, f))]
    total_cases = len(test_files)

    if total_cases == 0:
        return {"line_cov": 0.0, "branch_cov": 0.0, "crashes": 0, "total_cases": 0}

    # Batch execute: use a shell loop to run all test cases in one subprocess.
    # This avoids per-file Python subprocess fork overhead.
    # The shell script counts crash signals (retcode > 128).
    list_file = os.path.join(cov_dir, "_test_list.txt")
    with open(list_file, "w") as lf:
        for fp in test_files:
            lf.write(fp + "\n")

    if uses_file:
        run_cmd_part = f'"{cov_binary}" "$f"'
    else:
        run_cmd_part = f'"{cov_binary}" < "$f"'

    script = (
        f'crashes=0; '
        f'while IFS= read -r f; do '
        f'  {run_cmd_part} >/dev/null 2>&1; '
        f'  rc=$?; '
        f'  [ $rc -gt 128 ] && crashes=$((crashes+1)); '
        f'done < "{list_file}"; '
        f'echo "$crashes"'
    )

    try:
        # Allow generous timeout: 2s per case (most finish in <10ms)
        batch_timeout = max(60, total_cases * 2)
        result = subprocess.run(
            ["bash", "-c", script],
            capture_output=True, text=True, timeout=batch_timeout
        )
        if result.stdout.strip().isdigit():
            crashes = int(result.stdout.strip())
    except subprocess.TimeoutExpired:
        pass
    except Exception:
        pass

    # Clean up temp file
    try:
        os.remove(list_file)
    except OSError:
        pass

    # Run gcov to get coverage stats
    line_cov = 0.0
    branch_cov = 0.0

    try:
        result = subprocess.run(
            ["gcov", "-b", source_file],
            capture_output=True, text=True, cwd=cov_dir, timeout=30
        )
        output = result.stdout

        # Parse "Lines executed:XX.XX% of YY"
        m = re.search(r"Lines executed:(\d+\.\d+)% of (\d+)", output)
        if m:
            line_cov = float(m.group(1))

        # Parse "Taken at least once:XX.XX% of YY" for true branch coverage.
        # "Branches executed" only means the branch instruction was reached,
        # not that both outcomes (true/false) were covered.
        m = re.search(r"Taken at least once:(\d+\.\d+)% of (\d+)", output)
        if m:
            branch_cov = float(m.group(1))
        else:
            # Fall back to "Branches executed" if "Taken at least once" not found
            m = re.search(r"Branches executed:(\d+\.\d+)% of (\d+)", output)
            if m:
                branch_cov = float(m.group(1))
    except Exception:
        pass

    return {
        "line_cov": line_cov,
        "branch_cov": branch_cov,
        "crashes": crashes,
        "total_cases": total_cases,
    }


def get_seeds(target_name):
    """Get seed files for a target."""
    prefix = TARGETS[target_name][2]
    seeds = []
    for f in sorted(SEEDS_DIR.iterdir()):
        if f.name.startswith(prefix) and f.is_file():
            seeds.append(str(f))
    return seeds


def count_output_files(directory):
    """Count test case files in a directory."""
    if not os.path.isdir(directory):
        return 0
    count = 0
    for f in os.listdir(directory):
        if os.path.isfile(os.path.join(directory, f)):
            count += 1
    return count


def get_unique_hashes(directory):
    """Get set of unique file content hashes."""
    hashes = set()
    if not os.path.isdir(directory):
        return hashes
    for f in os.listdir(directory):
        fpath = os.path.join(directory, f)
        if os.path.isfile(fpath):
            with open(fpath, "rb") as fh:
                h = hashlib.sha256(fh.read()).hexdigest()
                hashes.add(h)
    return hashes


def run_serial(binary, target_name, seed_dir, timeout, work_dir):
    """Run the serial pure_concolic_execution.sh baseline."""
    output_dir = os.path.join(work_dir, "serial_output")
    os.makedirs(output_dir, exist_ok=True)

    uses_file = TARGETS[target_name][3] if target_name in TARGETS else True

    if uses_file:
        cmd = [
            "bash", str(SERIAL_SCRIPT),
            "-i", seed_dir,
            "-o", output_dir,
            binary, "@@"
        ]
    else:
        cmd = [
            "bash", str(SERIAL_SCRIPT),
            "-i", seed_dir,
            "-o", output_dir,
            binary
        ]

    # The serial script runs forever, so we use timeout
    timed_out = False
    start = time.monotonic()
    try:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        proc.wait(timeout=timeout)
        retcode = proc.returncode
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        retcode = -1
        timed_out = True

    elapsed = time.monotonic() - start
    num_generated = count_output_files(output_dir)
    unique = get_unique_hashes(output_dir)

    return {
        "wall_time": elapsed,
        "generated": num_generated,
        "unique": len(unique),
        "output_dir": output_dir,
        "retcode": retcode,
        "timed_out": timed_out,
    }


def run_mpi(binary, target_name, seed_dir, np, timeout, work_dir):
    """Run MPI-parallel concolic execution."""
    output_dir = os.path.join(work_dir, f"mpi_np{np}_output")
    os.makedirs(output_dir, exist_ok=True)

    uses_file = TARGETS[target_name][3] if target_name in TARGETS else True
    max_idle = max(10, timeout // 6)  # shorter idle wait for benchmarks

    # Give MPI script a wall timeout slightly less than the benchmark timeout
    # so it can shut down gracefully before the outer subprocess kills it.
    wall_timeout = max(10, timeout - 30)
    cmd = [
        "mpirun", "--allow-run-as-root", "--oversubscribe",
        "-np", str(np),
        "python3", str(MPI_SCRIPT),
        "-i", seed_dir,
        "-o", output_dir,
        "-t", str(min(30, timeout // 2)),
        "--max-idle", str(max_idle),
        "--wall-timeout", str(wall_timeout),
        "--", binary,
    ]

    if uses_file:
        cmd.append("@@")

    start = time.monotonic()
    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout + 30
        )
        stdout = proc.stdout
        stderr = proc.stderr
        retcode = proc.returncode
    except subprocess.TimeoutExpired:
        stdout = ""
        stderr = "TIMEOUT"
        retcode = -1

    elapsed = time.monotonic() - start
    timed_out = (retcode == -1 and stderr == "TIMEOUT") or elapsed >= timeout + 25

    # Parse the MPI master's stdout for pre-dedup total_generated count
    mpi_total_generated = None
    mpi_total_interesting = None
    if stdout:
        m = re.search(r"Total test cases generated:\s*(\d+)", stdout)
        if m:
            mpi_total_generated = int(m.group(1))
        m = re.search(r"New interesting test cases:\s*(\d+)", stdout)
        if m:
            mpi_total_interesting = int(m.group(1))

    unique = get_unique_hashes(output_dir)
    # Use MPI master's pre-dedup count if available; otherwise fall back to file count
    if mpi_total_generated is not None:
        num_generated = mpi_total_generated
    else:
        num_generated = count_output_files(output_dir)

    return {
        "wall_time": elapsed,
        "generated": num_generated,
        "unique": len(unique),
        "output_dir": output_dir,
        "stdout": stdout[-500:] if stdout else "",
        "stderr": stderr[-500:] if stderr else "",
        "retcode": retcode,
        "timed_out": timed_out,
    }


def format_time(seconds):
    """Format seconds as human-readable."""
    if seconds < 60:
        return f"{seconds:.1f}s"
    minutes = int(seconds // 60)
    secs = seconds % 60
    return f"{minutes}m{secs:.1f}s"


def generate_report(results, output_dir):
    """Generate the performance comparison report."""
    report_path = os.path.join(output_dir, "benchmark_report.txt")
    csv_path = os.path.join(output_dir, "benchmark_data.csv")
    json_path = os.path.join(output_dir, "benchmark_data.json")

    # CSV output
    with open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow([
            "target", "mode", "np", "round",
            "wall_time_sec", "generated", "unique",
            "line_cov_pct", "branch_cov_pct", "crashes",
            "speedup", "efficiency"
        ])
        for row in results:
            writer.writerow([
                row["target"], row["mode"], row["np"], row["round"],
                f"{row['wall_time']:.2f}", row["generated"], row["unique"],
                f"{row.get('line_cov', 0.0):.2f}",
                f"{row.get('branch_cov', 0.0):.2f}",
                row.get("crashes", 0),
                f"{row.get('speedup', 1.0):.2f}",
                f"{row.get('efficiency', 100.0):.1f}"
            ])

    # JSON output
    with open(json_path, "w") as f:
        json.dump(results, f, indent=2, default=str)

    # Text report
    with open(report_path, "w") as f:
        f.write("=" * 80 + "\n")
        f.write("  SymCC MPI Parallelization Benchmark Report\n")
        f.write(f"  Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}\n")
        f.write("=" * 80 + "\n\n")

        # Group by target
        by_target = defaultdict(lambda: defaultdict(list))
        for row in results:
            key = (row["mode"], row["np"])
            by_target[row["target"]][key].append(row)

        for target in sorted(by_target.keys()):
            configs = by_target[target]
            f.write(f"\n{'─' * 80}\n")
            f.write(f"  Target: {target}\n")
            f.write(f"{'─' * 80}\n\n")

            # Compute averages
            summaries = []
            for (mode, np_val), rows in sorted(configs.items()):
                avg_time = sum(r["wall_time"] for r in rows) / len(rows)
                avg_gen = sum(r["generated"] for r in rows) / len(rows)
                avg_uniq = sum(r["unique"] for r in rows) / len(rows)
                avg_line_cov = sum(r.get("line_cov", 0) for r in rows) / len(rows)
                avg_branch_cov = sum(r.get("branch_cov", 0) for r in rows) / len(rows)
                total_crashes = sum(r.get("crashes", 0) for r in rows)
                summaries.append({
                    "mode": mode,
                    "np": np_val,
                    "avg_time": avg_time,
                    "avg_generated": avg_gen,
                    "avg_unique": avg_uniq,
                    "avg_line_cov": avg_line_cov,
                    "avg_branch_cov": avg_branch_cov,
                    "total_crashes": total_crashes,
                    "rounds": len(rows),
                })

            # Find serial baseline time
            serial_time = None
            for s in summaries:
                if s["mode"] == "serial":
                    serial_time = s["avg_time"]
                    break

            # Check if any coverage data is present
            has_cov = any(s["avg_line_cov"] > 0 or s["avg_branch_cov"] > 0
                          for s in summaries)

            # Table header
            hdr = (f"  {'Mode':<10} {'NP':>4} {'Avg Time':>12} "
                   f"{'Generated':>10} {'Unique':>8} ")
            sep = (f"  {'─'*10} {'─'*4} {'─'*12} "
                   f"{'─'*10} {'─'*8} ")
            if has_cov:
                hdr += f"{'LineCov':>8} {'BranchCov':>10} {'Crashes':>8} "
                sep += f"{'─'*8} {'─'*10} {'─'*8} "
            hdr += f"{'Speedup':>8} {'Efficiency':>10}\n"
            sep += f"{'─'*8} {'─'*10}\n"
            f.write(hdr)
            f.write(sep)

            for s in summaries:
                if serial_time and serial_time > 0 and s["mode"] != "serial":
                    speedup = serial_time / s["avg_time"] if s["avg_time"] > 0 else 0
                    workers = s["np"] - 1  # subtract master
                    efficiency = (speedup / workers * 100) if workers > 0 else 0
                else:
                    speedup = 1.0
                    efficiency = 100.0

                line = (f"  {s['mode']:<10} {s['np']:>4} "
                        f"{format_time(s['avg_time']):>12} "
                        f"{s['avg_generated']:>10.1f} "
                        f"{s['avg_unique']:>8.1f} ")
                if has_cov:
                    line += (f"{s['avg_line_cov']:>7.1f}% "
                             f"{s['avg_branch_cov']:>9.1f}% "
                             f"{s['total_crashes']:>8} ")
                line += f"{speedup:>7.2f}x {efficiency:>9.1f}%\n"
                f.write(line)

            f.write("\n")

            # Coverage chart (ASCII) - most important metric
            if has_cov:
                f.write("  Branch Coverage Chart:\n")
                for s in summaries:
                    label = f"  np={s['np']:>2}" if s["mode"] != "serial" else "  serial"
                    cov = s["avg_branch_cov"]
                    bar_len = int(cov / 2.5)  # scale: 100% = 40 chars
                    bar = "█" * bar_len + "░" * max(0, 40 - bar_len)
                    f.write(f"  {label:>8} |{bar}| {cov:.1f}%\n")
                f.write("\n")

            # Speedup chart (ASCII)
            f.write("  Speedup Chart:\n")
            for s in summaries:
                if serial_time and serial_time > 0 and s["mode"] != "serial":
                    speedup = serial_time / s["avg_time"] if s["avg_time"] > 0 else 0
                else:
                    speedup = 1.0
                bar_len = int(speedup * 10)
                label = f"  np={s['np']:>2}"
                bar = "█" * bar_len + "░" * max(0, 40 - bar_len)
                f.write(f"  {label} |{bar}| {speedup:.2f}x\n")
            f.write("\n")

        # Overall summary
        f.write(f"\n{'=' * 80}\n")
        f.write(f"  OVERALL SUMMARY\n")
        f.write(f"{'=' * 80}\n\n")

        # Find best config per target (by coverage first, then throughput)
        for target in sorted(by_target.keys()):
            configs = by_target[target]
            best = None
            best_score = -1
            for (mode, np_val), rows in configs.items():
                avg_time = sum(r["wall_time"] for r in rows) / len(rows)
                avg_gen = sum(r["generated"] for r in rows) / len(rows)
                avg_branch_cov = sum(r.get("branch_cov", 0) for r in rows) / len(rows)
                total_crashes = sum(r.get("crashes", 0) for r in rows)
                throughput = avg_gen / avg_time if avg_time > 0 else 0
                # Score: prioritize coverage, then throughput
                score = avg_branch_cov * 1000 + throughput
                if score > best_score:
                    best_score = score
                    best = {
                        "mode": mode, "np": np_val,
                        "time": avg_time, "gen": avg_gen,
                        "throughput": throughput,
                        "branch_cov": avg_branch_cov,
                        "crashes": total_crashes,
                    }

            if best:
                info = (f"  {target}: best = {best['mode']} np={best['np']} "
                        f"({format_time(best['time'])}, "
                        f"{best['gen']:.0f} test cases, "
                        f"{best['throughput']:.1f} tc/s")
                if best["branch_cov"] > 0:
                    info += f", branch_cov={best['branch_cov']:.1f}%"
                if best["crashes"] > 0:
                    info += f", crashes={best['crashes']}"
                info += ")\n"
                f.write(info)

        f.write(f"\n  Report files:\n")
        f.write(f"    Text:  {report_path}\n")
        f.write(f"    CSV:   {csv_path}\n")
        f.write(f"    JSON:  {json_path}\n")
        f.write(f"\n{'=' * 80}\n")

    return report_path


def main():
    parser = argparse.ArgumentParser(
        description="SymCC MPI Parallelization Benchmark"
    )
    parser.add_argument("--symcc", default=None,
                        help="Path to SymCC compiler")
    parser.add_argument("--np-list", default="1,2,4,8",
                        help="Comma-separated process counts (default: 1,2,4,8)")
    parser.add_argument("--targets", default=None,
                        help="Comma-separated target names (default: all)")
    parser.add_argument("--rounds", type=int, default=3,
                        help="Rounds per configuration (default: 3)")
    parser.add_argument("--timeout", type=int, default=60,
                        help="Timeout per run in seconds (default: 60)")
    parser.add_argument("--output", default="benchmark_results",
                        help="Output directory (default: benchmark_results)")
    parser.add_argument("--skip-build", action="store_true",
                        help="Skip compilation step")
    parser.add_argument("--simulation", action="store_true",
                        help="Use gcc instead of SymCC (tests MPI framework only)")
    parser.add_argument("--public", nargs="*", metavar="NAME:BINARY:SEEDDIR",
                        help="Add public benchmark targets. "
                             "With no args: auto-discover compiled targets in benchmark/public/bin/. "
                             "With args: name:binary_path:seed_dir "
                             "e.g., 'file:./benchmark/public/bin/lava/file:./benchmark/public/seeds/lava/file'.")
    parser.add_argument("--no-coverage", action="store_true",
                        help="Skip coverage measurement (faster but less metrics)")

    args = parser.parse_args()

    np_list = [int(x) for x in args.np_list.split(",")]
    target_names = args.targets.split(",") if args.targets else list(TARGETS.keys())

    output_dir = os.path.abspath(args.output)
    bin_dir = os.path.join(output_dir, "bin")
    os.makedirs(output_dir, exist_ok=True)

    print("=" * 70)
    print("  SymCC MPI Parallelization Benchmark")
    print("=" * 70)
    print(f"  Targets:     {', '.join(target_names)}")
    print(f"  NP values:   {np_list}")
    print(f"  Rounds:      {args.rounds}")
    print(f"  Timeout:     {args.timeout}s per run")
    print(f"  Coverage:    {'enabled' if not args.no_coverage else 'disabled'}")
    print(f"  Output:      {output_dir}")
    print()

    # Build step
    if not args.skip_build:
        print("Step 1: Compiling target programs")
        print("-" * 40)

        if args.simulation:
            print("  (Simulation mode: using gcc)")
            binaries = build_targets_gcc(bin_dir)
        else:
            symcc = args.symcc or find_symcc()
            if symcc:
                print(f"  Using SymCC: {symcc}")
                binaries = build_targets(symcc, bin_dir)
            else:
                print("  SymCC not found, falling back to gcc (simulation mode)")
                print("  NOTE: simulation mode tests the MPI framework overhead,")
                print("        not actual symbolic execution performance.")
                binaries = build_targets_gcc(bin_dir)
                args.simulation = True
    else:
        # Find existing binaries
        binaries = {}
        for name in target_names:
            for suffix in ["_symcc", "_native"]:
                path = os.path.join(bin_dir, f"{name}{suffix}")
                if os.path.isfile(path):
                    binaries[name] = path
                    break

    # Add public benchmark targets (--public [name:binary:seeddir ...])
    public_targets = {}
    public_seed_dirs = {}
    if args.public is not None:
        print("\n  Adding public benchmark targets:")

        public_specs = list(args.public)  # explicit specs from CLI

        # If no explicit specs given, auto-discover from benchmark/public/bin/
        if not public_specs:
            pub_bin_dir = PUBLIC_DIR / "bin"
            pub_seed_dir = PUBLIC_DIR / "seeds"
            if pub_bin_dir.is_dir():
                for suite_dir in sorted(pub_bin_dir.iterdir()):
                    if not suite_dir.is_dir():
                        continue
                    for binary in sorted(suite_dir.iterdir()):
                        if binary.is_file() and os.access(str(binary), os.X_OK):
                            bname = binary.name
                            seed_candidate = pub_seed_dir / suite_dir.name / bname
                            if seed_candidate.is_dir():
                                public_specs.append(
                                    f"{bname}:{binary}:{seed_candidate}"
                                )
                if not public_specs:
                    print("    No compiled public benchmarks found in:")
                    print(f"      {pub_bin_dir}/")
                    print("    Run first: ./compile_public_benchmarks.sh --all")
            else:
                print(f"    Public bin directory not found: {pub_bin_dir}")
                print("    Run first:")
                print("      ./setup_public_benchmarks.sh --lava")
                print("      ./compile_public_benchmarks.sh --lava")

        for spec in public_specs:
            parts = spec.split(":")
            if len(parts) != 3:
                print(f"    WARNING: invalid format '{spec}', expected name:binary:seeddir")
                continue
            name, binary_path, seed_path = parts

            # Resolve paths: try as-is first, then relative to SCRIPT_DIR
            binary_path = _resolve_path(binary_path)
            seed_path = _resolve_path(seed_path)

            if not os.path.isfile(binary_path):
                print(f"    WARNING: binary not found: {binary_path}")
                continue
            if not os.path.isdir(seed_path):
                print(f"    WARNING: seed dir not found: {seed_path}")
                continue
            binaries[name] = binary_path
            public_seed_dirs[name] = seed_path
            public_targets[name] = True
            if name not in target_names:
                target_names.append(name)
            print(f"    {name}: {binary_path} (seeds: {seed_path})")

    if not binaries:
        print("\nERROR: No target binaries available.")
        sys.exit(1)

    available_targets = [t for t in target_names if t in binaries]
    print(f"\n  Available targets: {', '.join(available_targets)}")

    # Check MPI
    if not shutil.which("mpirun"):
        print("\nERROR: mpirun not found. Install OpenMPI: apt install openmpi-bin")
        sys.exit(1)

    # Build coverage binaries (for built-in targets only)
    cov_binaries = {}
    cov_dirs = {}
    enable_coverage = not args.no_coverage
    if enable_coverage:
        if not shutil.which("gcov"):
            print("\n  WARNING: gcov not found, disabling coverage measurement")
            enable_coverage = False
        else:
            print("\n  Building coverage-instrumented binaries:")
            cov_bin_dir = os.path.join(output_dir, "cov_bin")
            cov_binaries, cov_dirs = build_coverage_targets(cov_bin_dir)
            if not cov_binaries:
                print("  WARNING: no coverage binaries built, disabling coverage")
                enable_coverage = False

    # Prepare seed directories per target
    seed_dirs = {}
    for target in available_targets:
        if target in public_seed_dirs:
            # Public benchmark: use the provided seed directory directly
            seed_dirs[target] = public_seed_dirs[target]
        elif target in TARGETS:
            # Built-in benchmark: copy matching seeds
            prefix = TARGETS[target][2]
            target_seed_dir = os.path.join(output_dir, f"seeds_{target}")
            os.makedirs(target_seed_dir, exist_ok=True)
            for f in SEEDS_DIR.iterdir():
                if f.name.startswith(prefix):
                    shutil.copy2(str(f), target_seed_dir)
            seed_dirs[target] = target_seed_dir

    # Run benchmarks
    print(f"\nStep 2: Running benchmarks")
    print("-" * 40)

    all_results = []
    total_runs = len(available_targets) * (1 + len(np_list)) * args.rounds
    current_run = 0

    for target in available_targets:
        binary = binaries[target]
        seed_dir = seed_dirs[target]

        print(f"\n  Target: {target}")
        print(f"  Binary: {binary}")
        print(f"  Seeds:  {seed_dir} ({len(os.listdir(seed_dir))} files)")

        # Serial baseline
        print(f"\n  [Serial baseline]")
        for r in range(args.rounds):
            current_run += 1
            work_dir = tempfile.mkdtemp(prefix=f"bench_{target}_serial_r{r}_")

            print(f"    Round {r+1}/{args.rounds}... ", end="", flush=True)
            result = run_serial(binary, target, seed_dir, args.timeout, work_dir)

            # Measure coverage before cleanup
            cov_data = {"line_cov": 0.0, "branch_cov": 0.0, "crashes": 0}
            if enable_coverage and target in cov_binaries:
                uses_file = TARGETS[target][3] if target in TARGETS else True
                cov_data = measure_coverage(
                    cov_binaries[target], cov_dirs[target],
                    TARGETS[target][0], result["output_dir"],
                    uses_file=uses_file
                )

            cov_str = ""
            if enable_coverage and target in cov_binaries:
                cov_str = (f", line={cov_data['line_cov']:.1f}%, "
                           f"branch={cov_data['branch_cov']:.1f}%, "
                           f"crashes={cov_data['crashes']}")
            timeout_str = ""
            if result.get("timed_out"):
                timeout_str = " [TIMEOUT]"
            print(f"time={format_time(result['wall_time'])}, "
                  f"gen={result['generated']}, uniq={result['unique']}, "
                  f"ret={result.get('retcode', '?')}"
                  f"{cov_str}{timeout_str}")

            all_results.append({
                "target": target,
                "mode": "serial",
                "np": 1,
                "round": r + 1,
                "wall_time": result["wall_time"],
                "generated": result["generated"],
                "unique": result["unique"],
                "line_cov": cov_data["line_cov"],
                "branch_cov": cov_data["branch_cov"],
                "crashes": cov_data["crashes"],
            })

            shutil.rmtree(work_dir, ignore_errors=True)

        # MPI parallel
        for np_val in np_list:
            if np_val < 2:
                # np=1 doesn't make sense for MPI (need master + 1 worker)
                # Use np=2 instead
                actual_np = 2
            else:
                actual_np = np_val

            print(f"\n  [MPI np={actual_np} ({actual_np-1} workers)]")
            for r in range(args.rounds):
                current_run += 1
                work_dir = tempfile.mkdtemp(
                    prefix=f"bench_{target}_mpi{actual_np}_r{r}_"
                )

                print(f"    Round {r+1}/{args.rounds}... ", end="", flush=True)
                result = run_mpi(
                    binary, target, seed_dir, actual_np,
                    args.timeout, work_dir
                )

                # Measure coverage before cleanup
                cov_data = {"line_cov": 0.0, "branch_cov": 0.0, "crashes": 0}
                if enable_coverage and target in cov_binaries:
                    uses_file = TARGETS[target][3] if target in TARGETS else True
                    cov_data = measure_coverage(
                        cov_binaries[target], cov_dirs[target],
                        TARGETS[target][0], result["output_dir"],
                        uses_file=uses_file
                    )

                cov_str = ""
                if enable_coverage and target in cov_binaries:
                    cov_str = (f", line={cov_data['line_cov']:.1f}%, "
                               f"branch={cov_data['branch_cov']:.1f}%, "
                               f"crashes={cov_data['crashes']}")
                timeout_str = ""
                if result.get("timed_out"):
                    timeout_str = " [TIMEOUT]"
                print(f"time={format_time(result['wall_time'])}, "
                      f"gen={result['generated']}, uniq={result['unique']}, "
                      f"ret={result.get('retcode', '?')}"
                      f"{cov_str}{timeout_str}")
                # Print stderr summary for non-zero retcodes to aid diagnosis
                retcode = result.get("retcode", 0)
                stderr_text = result.get("stderr", "")
                if retcode != 0 and stderr_text and stderr_text != "TIMEOUT":
                    # Show last few meaningful lines
                    err_lines = [l for l in stderr_text.strip().splitlines() if l.strip()]
                    if err_lines:
                        print(f"      stderr: {err_lines[-1][:200]}")

                # Compute speedup against serial baseline
                serial_avg = 0
                serial_count = 0
                for sr in all_results:
                    if sr["target"] == target and sr["mode"] == "serial":
                        serial_avg += sr["wall_time"]
                        serial_count += 1
                if serial_count > 0:
                    serial_avg /= serial_count
                    speedup = serial_avg / result["wall_time"] if result["wall_time"] > 0 else 0
                    workers = actual_np - 1
                    efficiency = (speedup / workers * 100) if workers > 0 else 0
                else:
                    speedup = 1.0
                    efficiency = 100.0

                all_results.append({
                    "target": target,
                    "mode": "mpi",
                    "np": actual_np,
                    "round": r + 1,
                    "wall_time": result["wall_time"],
                    "generated": result["generated"],
                    "unique": result["unique"],
                    "line_cov": cov_data["line_cov"],
                    "branch_cov": cov_data["branch_cov"],
                    "crashes": cov_data["crashes"],
                    "speedup": speedup,
                    "efficiency": efficiency,
                })

                shutil.rmtree(work_dir, ignore_errors=True)

    # Generate report
    print(f"\n\nStep 3: Generating report")
    print("-" * 40)
    report_path = generate_report(all_results, output_dir)

    # Print the report to stdout
    with open(report_path) as f:
        print(f.read())

    print(f"\nBenchmark complete. Results in: {output_dir}/")


if __name__ == "__main__":
    main()
