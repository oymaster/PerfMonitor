#!/usr/bin/env bash
set -u

OUT_DIR="${OUT_DIR:-benchmarks/cpu_metric_accuracy_$(date +%Y%m%d_%H%M%S)}"
MANAGER_HTTP="${MANAGER_HTTP:-http://127.0.0.1:8080}"
WORKER_PID="${WORKER_PID:-}"
SAMPLES="${SAMPLES:-12}"
WARMUP_S="${WARMUP_S:-5}"

mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/cpu_accuracy.csv"

if [[ -z "$WORKER_PID" ]]; then
  WORKER_PID="$(pgrep -n worker || true)"
fi

echo 'phase,sample,mpstat_used_pct,monitor_cpu_used_pct,abs_error_pct,load1,worker_cpu_pct,worker_rss_kb' > "$OUT"
load_pids=()

cleanup() {
  if ((${#load_pids[@]})); then
    kill "${load_pids[@]}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

read_metric_value() {
  local name="$1"
  awk -v n="$name" '$1 ~ ("^" n) {print $2; exit}'
}

run_phase() {
  local phase="$1"
  local samples="$2"
  echo "phase=$phase" >&2
  sleep "$WARMUP_S"
  for i in $(seq 1 "$samples"); do
    local mp_line idle mp_used metrics mon load1 ps_line worker_cpu worker_rss err

    mp_line="$(LC_ALL=C mpstat 1 1 | awk '/Average:/ && $2 == "all" {print}' || true)"
    idle="$(printf '%s\n' "$mp_line" | awk '{print $NF}')"
    if [[ -n "$idle" ]]; then
      mp_used="$(awk -v idle="$idle" 'BEGIN {printf "%.2f", 100-idle}')"
    else
      mp_used=nan
    fi

    metrics="$(curl -sf --max-time 3 "$MANAGER_HTTP/metrics" || true)"
    mon="$(printf '%s\n' "$metrics" | read_metric_value monitor_cpu_used_pct)"
    load1="$(printf '%s\n' "$metrics" | read_metric_value monitor_cpu_load_1m)"
    [[ -n "$mon" ]] || mon=nan
    [[ -n "$load1" ]] || load1=nan

    if [[ -n "$WORKER_PID" ]]; then
      ps_line="$(ps -p "$WORKER_PID" -o pcpu=,rss= || true)"
      read -r worker_cpu worker_rss <<<"$ps_line"
    else
      worker_cpu=nan
      worker_rss=nan
    fi
    [[ -n "${worker_cpu:-}" ]] || worker_cpu=nan
    [[ -n "${worker_rss:-}" ]] || worker_rss=nan

    err="$(awk -v a="$mp_used" -v b="$mon" 'BEGIN {if (a == "nan" || b == "nan") {print "nan"; exit}; d=a-b; if (d<0) d=-d; printf "%.2f", d}')"
    echo "$phase,$i,$mp_used,$mon,$err,$load1,$worker_cpu,$worker_rss" >> "$OUT"
  done
}

run_phase idle "$SAMPLES"

yes >/dev/null 2>&1 &
load_pids=("$!")
run_phase yes1 "$SAMPLES"
cleanup
load_pids=()

yes >/dev/null 2>&1 &
load_pids=("$!")
yes >/dev/null 2>&1 &
load_pids+=("$!")
run_phase yes2 "$SAMPLES"
cleanup
load_pids=()

python3 - "$OUT" <<'PY'
import csv
import math
import pathlib
import statistics
import sys

path = pathlib.Path(sys.argv[1])
rows = list(csv.DictReader(path.open()))
print(f"csv={path}")
for phase in ("idle", "yes1", "yes2"):
    rs = [r for r in rows if r["phase"] == phase]
    print("==", phase, "samples", len(rs))
    for key in ("mpstat_used_pct", "monitor_cpu_used_pct", "abs_error_pct", "worker_cpu_pct", "worker_rss_kb"):
        vals = []
        for row in rs:
            try:
                v = float(row[key])
            except ValueError:
                continue
            if not math.isnan(v):
                vals.append(v)
        if not vals:
            print(key, "valid", 0)
            continue
        vals_sorted = sorted(vals)
        p95 = vals_sorted[int(0.95 * (len(vals_sorted) - 1))]
        print(
            key,
            "avg", round(statistics.fmean(vals), 2),
            "median", round(statistics.median(vals), 2),
            "p95", round(p95, 2),
            "max", round(max(vals), 2),
        )
PY
