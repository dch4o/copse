#!/usr/bin/env bash
# Simple local benchmark regression check.
#
# Runs the copse benchmarks and compares each benchmark's mean against a
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

for bench in bench_insert bench_search bench_rebuild bench_remove; do
  bin=$(find "$build" -name "$bench" -type f -perm -u+x | head -1)
  if [ -z "$bin" ]; then
    echo "missing benchmark binary '$bench' under '$build' — build first" >&2
    exit 2
  fi
  "$bin" "[!benchmark]" --reporter xml --benchmark-samples "$samples" \
    --out "$tmp/${bench}.xml"
done

# Emit "<binary>\t<benchmark>\t<mean_ns>" for every benchmark, sorted.
extract() {
  # A BENCHMARK name is only unique within its TEST_CASE, so qualify it with
  # the case name. <TestCase ...> carries a filename="..." attribute that also
  # contains name=", so anchor the match to the element to avoid grabbing it.
  awk '
    function bin(f,  b){ b=f; sub(/.*\//,"",b); sub(/\.xml$/,"",b); return b }
    /<TestCase /        { tc=$0; sub(/.*<TestCase name="/,"",tc); sub(/".*/,"",tc); sub(/^Bench: /,"",tc) }
    /<BenchmarkResults / { nm=$0; sub(/.*<BenchmarkResults name="/,"",nm); sub(/".*/,"",nm) }
    /<mean /            { v=$0; sub(/.*value="/,"",v); sub(/".*/,"",v);
                          printf "%s\t%s :: %s\t%s\n", bin(FILENAME), tc, nm, v }
  ' "$tmp"/*.xml | sort
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
' "$baseline" "$tmp/current.tsv" | column -t -s "$(printf '\t')"
