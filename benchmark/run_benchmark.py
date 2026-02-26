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
import json
import os
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
    import hashlib
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
    start = time.monotonic()
    try:
        proc = subprocess.Popen(
            cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()

    elapsed = time.monotonic() - start
    num_generated = count_output_files(output_dir)
    unique = get_unique_hashes(output_dir)

    return {
        "wall_time": elapsed,
        "generated": num_generated,
        "unique": len(unique),
    }


def run_mpi(binary, target_name, seed_dir, np, timeout, work_dir):
    """Run MPI-parallel concolic execution."""
    output_dir = os.path.join(work_dir, f"mpi_np{np}_output")
    os.makedirs(output_dir, exist_ok=True)

    uses_file = TARGETS[target_name][3] if target_name in TARGETS else True
    max_idle = max(10, timeout // 6)  # shorter idle wait for benchmarks

    cmd = [
        "mpirun", "--allow-run-as-root", "--oversubscribe",
        "-np", str(np),
        "python3", str(MPI_SCRIPT),
        "-i", seed_dir,
        "-o", output_dir,
        "-t", str(min(30, timeout // 2)),
        "--max-idle", str(max_idle),
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
    num_generated = count_output_files(output_dir)
    unique = get_unique_hashes(output_dir)

    return {
        "wall_time": elapsed,
        "generated": num_generated,
        "unique": len(unique),
        "stdout": stdout[-500:] if stdout else "",
        "retcode": retcode,
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
            "speedup", "efficiency"
        ])
        for row in results:
            writer.writerow([
                row["target"], row["mode"], row["np"], row["round"],
                f"{row['wall_time']:.2f}", row["generated"], row["unique"],
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
                summaries.append({
                    "mode": mode,
                    "np": np_val,
                    "avg_time": avg_time,
                    "avg_generated": avg_gen,
                    "avg_unique": avg_uniq,
                    "rounds": len(rows),
                })

            # Find serial baseline time
            serial_time = None
            for s in summaries:
                if s["mode"] == "serial":
                    serial_time = s["avg_time"]
                    break

            # Table header
            f.write(f"  {'Mode':<10} {'NP':>4} {'Avg Time':>12} "
                    f"{'Generated':>10} {'Unique':>8} "
                    f"{'Speedup':>8} {'Efficiency':>10}\n")
            f.write(f"  {'─'*10} {'─'*4} {'─'*12} "
                    f"{'─'*10} {'─'*8} "
                    f"{'─'*8} {'─'*10}\n")

            for s in summaries:
                if serial_time and serial_time > 0 and s["mode"] != "serial":
                    speedup = serial_time / s["avg_time"] if s["avg_time"] > 0 else 0
                    workers = s["np"] - 1  # subtract master
                    efficiency = (speedup / workers * 100) if workers > 0 else 0
                else:
                    speedup = 1.0
                    efficiency = 100.0

                f.write(f"  {s['mode']:<10} {s['np']:>4} "
                        f"{format_time(s['avg_time']):>12} "
                        f"{s['avg_generated']:>10.1f} "
                        f"{s['avg_unique']:>8.1f} "
                        f"{speedup:>7.2f}x "
                        f"{efficiency:>9.1f}%\n")

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

        # Find best config per target
        for target in sorted(by_target.keys()):
            configs = by_target[target]
            best = None
            best_throughput = 0
            for (mode, np_val), rows in configs.items():
                avg_time = sum(r["wall_time"] for r in rows) / len(rows)
                avg_gen = sum(r["generated"] for r in rows) / len(rows)
                throughput = avg_gen / avg_time if avg_time > 0 else 0
                if throughput > best_throughput:
                    best_throughput = throughput
                    best = (mode, np_val, avg_time, avg_gen)

            if best:
                f.write(f"  {target}: best = {best[0]} np={best[1]} "
                        f"({format_time(best[2])}, {best[3]:.0f} test cases, "
                        f"{best_throughput:.1f} tc/s)\n")

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
    parser.add_argument("--public", nargs="*", metavar="BINARY:SEEDDIR",
                        help="Add public benchmark targets. Format: name:binary_path:seed_dir "
                             "e.g., 'openjpeg:./opj_decompress:./seeds/openjpeg'. "
                             "Use @@ in target args. Can specify multiple.")

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

    # Add public benchmark targets (--public name:binary:seeddir)
    public_targets = {}
    public_seed_dirs = {}
    if args.public:
        print("\n  Adding public benchmark targets:")
        for spec in args.public:
            parts = spec.split(":")
            if len(parts) != 3:
                print(f"    WARNING: invalid format '{spec}', expected name:binary:seeddir")
                continue
            name, binary_path, seed_path = parts
            binary_path = os.path.abspath(binary_path)
            seed_path = os.path.abspath(seed_path)
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
            print(f"time={format_time(result['wall_time'])}, "
                  f"gen={result['generated']}, uniq={result['unique']}")

            all_results.append({
                "target": target,
                "mode": "serial",
                "np": 1,
                "round": r + 1,
                "wall_time": result["wall_time"],
                "generated": result["generated"],
                "unique": result["unique"],
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
                print(f"time={format_time(result['wall_time'])}, "
                      f"gen={result['generated']}, uniq={result['unique']}, "
                      f"ret={result.get('retcode', '?')}")

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
