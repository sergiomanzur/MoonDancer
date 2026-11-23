"""Coverage-guided trace expansion for the Moonwalker recompiler.

Runs the trace-enabled binary with deterministic random inputs, records every
new (FD1094 state, PC) pair, merges the coverage traces, regenerates the C
translation, rebuilds, and repeats until the seeded runs no longer fall back.
"""
import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TRACE = ROOT / "src" / "generated_game.c"


def run(cmd, capture=True):
    print("$", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], cwd=ROOT, text=True,
                          capture_output=capture)


def count_misses(text):
    return len(re.findall(r"REC MISS", text))


def merge_traces(paths, out):
    keys = set()
    for path in paths:
        p = Path(path)
        if not p.exists():
            continue
        for line in p.read_text().splitlines():
            if line.strip():
                st, pc = line.split()
                keys.add((int(st, 16), int(pc, 16)))
    out.write_text("\n".join(f"{st:02X} {pc:06X}" for st, pc in sorted(keys)) + "\n")
    return len(keys)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rom-dir", default="roms")
    ap.add_argument("--frames", type=int, default=3000)
    ap.add_argument("--seeds", default="1,2,3,4")
    ap.add_argument("--verify-seeds", default="5,6,7,8,9,10")
    ap.add_argument("--iterations", type=int, default=8)
    ap.add_argument("--build", default="build")
    ap.add_argument("--trace-build", default="build_trace")
    args = ap.parse_args()

    seeds = [int(s) for s in args.seeds.split(",") if s.strip()]
    verify_seeds = [int(s) for s in args.verify_seeds.split(",") if s.strip()]
    all_seeds = []
    for seed in seeds + verify_seeds:
        if seed not in all_seeds:
            all_seeds.append(seed)

    base = Path("build/trace_base.txt")
    original = Path("build/trace.txt")
    if not base.exists():
        shutil.copyfile(original, base)

    coverage_files = []

    for iteration in range(1, args.iterations + 1):
        print(f"\n=== expansion iteration {iteration} ===", flush=True)
        total_misses = 0
        for seed in all_seeds:
            cov = Path(f"build/trace_cov_{seed}.txt")
            exe = ROOT / args.trace_build / "mwalk_headless.exe"
            if not exe.exists():
                print(f"missing {exe}; build the trace target first", file=sys.stderr)
                return 1
            res = run([exe, "--rom-dir", args.rom_dir, "--frames", args.frames,
                       "--random", seed, "--record", cov])
            misses = count_misses(res.stderr)
            total_misses += misses
            print(f"seed {seed}: {misses} misses, record {cov}", flush=True)
            if cov.exists() and cov not in coverage_files:
                coverage_files.append(cov)

        paths = [base] + coverage_files
        merged = merge_traces(paths, original)
        print(f"merged {len(paths)} traces -> {merged} instruction keys", flush=True)

        gen = run([sys.executable, "tools/gen_recomp.py", str(original)])
        if gen.stdout:
            print(gen.stdout.strip(), flush=True)
        if gen.returncode != 0:
            print(gen.stderr, file=sys.stderr)
            return 1

        for build in (args.trace_build, args.build):
            b = run(["cmake", "--build", build, "--target", "mwalk_headless"])
            if b.returncode != 0:
                print(b.stdout)
                print(b.stderr, file=sys.stderr)
                return 1

        if total_misses == 0:
            print(f"seeded runs converged after iteration {iteration}", flush=True)
            break
    else:
        print("iteration limit reached without convergence", file=sys.stderr)
        return 2

    print("\n=== final no-record sweep ===", flush=True)
    verify_misses = 0
    for seed in all_seeds:
        exe = ROOT / args.build / "mwalk_headless.exe"
        res = run([exe, "--rom-dir", args.rom_dir, "--frames", args.frames,
                   "--random", seed])
        m = count_misses(res.stderr)
        verify_misses += m
        print(f"verify seed {seed}: {m} misses", flush=True)
    if verify_misses:
        print(f"final sweep found {verify_misses} misses", file=sys.stderr)
        return 3
    print("final sweep passed: no fallbacks", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
