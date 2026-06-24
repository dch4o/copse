#!/usr/bin/env bash
# Simple local benchmark regression check.
#
# Runs the `bench`-labelled ctest benchmarks and compares each mean against a
# committed baseline (docs/benchmark/baseline.tsv), flagging rows that got
# slower than the baseline by more than the threshold. Benchmark numbers are
# hardware-specific, so run this on the SAME machine that produced the
# baseline. Run once with --update to (re)capture the baseline after an
# intentional change.
#
# Usage:
#   tools/bench_regression.sh [--build DIR] [--samples N] [--threshold PCT] [--update]
#
# Build the benchmarks first, e.g.:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCOPSE_BUILD_BENCHMARKS=ON
#   cmake --build build
#
# Defaults: --build build  --samples 10  --threshold 25  (percent)
set -euo pipefail

build=build
samples=10
threshold=25
update=0
while [ $# -gt 0 ]; do
  case "$1" in
    --build)     build=$2; shift 2 ;;
    --samples)   samples=$2; shift 2 ;;
    --threshold) threshold=$2; shift 2 ;;
    --update)    update=1; shift ;;
    -h|--help)   sed -n '2,18p' "$0"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

repo_root=$(cd "$(dirname "$0")/.." && pwd)
baseline="$repo_root/docs/benchmark/baseline.tsv"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# Discover the benchmark binaries from the `bench` ctest label — the single
# source of truth, so a newly added bench is picked up here automatically.
mapfile -t benches < <(ctest --test-dir "$build" -L bench -N 2>/dev/null \
  | awk '/Test #[0-9]+: /{print $NF}')
if [ "${#benches[@]}" -eq 0 ]; then
  echo "no 'bench'-labelled tests under '$build' — configure with" \
       "-DCOPSE_BUILD_BENCHMARKS=ON and build first" >&2
  exit 2
fi

for bench in "${benches[@]}"; do
  bin=$(find "$build" -name "$bench" -type f -perm -u+x | head -1)
  if [ -z "$bin" ]; then
    echo "missing benchmark binary '$bench' under '$build' — build first" >&2
    exit 2
  fi
  "$bin" "[!benchmark]" --reporter xml --benchmark-samples "$samples" \
    --out "$tmp/${bench}.xml"
done

# Emit "<binary>\t<case> :: <benchmark>\t<mean_ns>" for every benchmark, sorted.
# Parse the Catch2 XML reporter with a real XML parser: each <TestCase> wraps
# <BenchmarkResults name="..."> holding a <mean value="..."/>. A BENCHMARK name
# is only unique within its TEST_CASE, so the key qualifies it with the case.
extract() {
  python3 - "$tmp"/*.xml <<'PY'
import os, sys, xml.etree.ElementTree as ET
rows = []
for path in sys.argv[1:]:
    binary = os.path.basename(path)[:-len(".xml")]
    for case in ET.parse(path).getroot().iter("TestCase"):
        name = case.get("name", "").removeprefix("Bench: ")
        for result in case.iter("BenchmarkResults"):
            mean = result.find("mean")
            if mean is not None:
                rows.append((binary, f"{name} :: {result.get('name', '')}", mean.get("value", "")))
for row in sorted(rows):
    print("\t".join(row))
PY
}

if [ "$update" = 1 ]; then
  extract > "$baseline"
  echo "baseline updated: $baseline ($(wc -l < "$baseline") rows)"
  exit 0
fi

if [ ! -f "$baseline" ]; then
  echo "no baseline at $baseline — run with --update first" >&2
  exit 2
fi

extract > "$tmp/current.tsv"

awk -v thr="$threshold" '
  BEGIN { FS="\t"; print "binary\tbenchmark\tbase(ns)\tcur(ns)\tdelta%\tstatus" }
  NR==FNR { base[$1 SUBSEP $2]=$3; next }
  {
    key=$1 SUBSEP $2; b=base[key]; c=$3
    if (b=="") { printf "%s\t%s\t-\t%.1f\t-\tNEW\n", $1, $2, c; next }
    d=100*(c-b)/b
    st=(d>thr)?"REGRESSION":((d<-thr)?"improved":"ok")
    if (st=="REGRESSION") fail=1
    printf "%s\t%s\t%.1f\t%.1f\t%+.1f\t%s\n", $1, $2, b, c, d, st
  }
  END { exit fail+0 }
' "$baseline" "$tmp/current.tsv" | { column -t -s "$(printf '\t')" 2>/dev/null || cat; }
