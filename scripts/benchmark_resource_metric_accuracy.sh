#!/usr/bin/env bash
set -u

OUT_DIR="${OUT_DIR:-benchmarks/resource_metric_accuracy_$(date +%Y%m%d_%H%M%S)}"
MANAGER_HTTP="${MANAGER_HTTP:-http://127.0.0.1:8080}"
SAMPLES="${SAMPLES:-10}"
WARMUP_S="${WARMUP_S:-5}"
MEM_ALLOC_MIB="${MEM_ALLOC_MIB:-256}"
DISK_DIR="${DISK_DIR:-/tmp/perfmonitor_resource_metric_accuracy}"

mkdir -p "$OUT_DIR" "$DISK_DIR"
OUT="$OUT_DIR/resource_accuracy.csv"

echo 'phase,sample,ref_mem_used_pct,monitor_mem_used_pct,mem_abs_error_pct,ref_disk_iops,monitor_disk_iops,disk_abs_error,ref_net_rx_mbps,ref_net_tx_mbps,monitor_net_rx_mbps,monitor_net_tx_mbps' > "$OUT"

load_pids=()
cleanup() {
  if ((${#load_pids[@]})); then
    kill "${load_pids[@]}" >/dev/null 2>&1 || true
  fi
  rm -f "$DISK_DIR/io_test.bin" >/dev/null 2>&1 || true
}
trap cleanup EXIT

metric() {
  local name="$1"
  awk -v n="$name" '$1 ~ ("^" n) {print $2; exit}'
}

mem_ref() {
  awk '
    $1=="MemTotal:" {total=$2}
    $1=="MemFree:" {free=$2}
    $1=="Cached:" {cached=$2}
    $1=="Buffers:" {buffers=$2}
    END {
      if (total > 0) {
        used=total-free-cached-buffers;
        printf "%.4f", used*100.0/total;
      } else {
        print "nan";
      }
    }
  ' /proc/meminfo
}

disk_raw() {
  awk '
    $3 !~ /^(loop|ram)/ {
      reads += $4;
      writes += $8;
    }
    END { printf "%.0f\n", reads+writes }
  ' /proc/diskstats
}

net_raw() {
  awk -F'[: ]+' '
    NR > 2 && $2 != "lo" {
      rx += $3;
      tx += $11;
    }
    END { printf "%.0f %.0f\n", rx, tx }
  ' /proc/net/dev
}

sample_once() {
  local phase="$1"
  local i="$2"
  local disk0 disk1 ref_disk net0_rx net0_tx net1_rx net1_tx ref_rx ref_tx

  disk0="$(disk_raw)"
  read -r net0_rx net0_tx <<<"$(net_raw)"
  sleep 1
  disk1="$(disk_raw)"
  read -r net1_rx net1_tx <<<"$(net_raw)"

  ref_disk="$(awk -v a="$disk0" -v b="$disk1" 'BEGIN {printf "%.2f", b-a}')"
  ref_rx="$(awk -v a="$net0_rx" -v b="$net1_rx" 'BEGIN {printf "%.4f", (b-a)*8.0/1024/1024}')"
  ref_tx="$(awk -v a="$net0_tx" -v b="$net1_tx" 'BEGIN {printf "%.4f", (b-a)*8.0/1024/1024}')"

  local metrics mon_mem mon_disk mon_rx mon_tx ref_mem mem_err disk_err
  metrics="$(curl -sf --max-time 3 "$MANAGER_HTTP/metrics" || true)"
  mon_mem="$(printf '%s\n' "$metrics" | metric monitor_mem_used_pct)"
  mon_disk="$(printf '%s\n' "$metrics" | metric monitor_disk_iops)"
  mon_rx="$(printf '%s\n' "$metrics" | metric monitor_net_rx_mbps)"
  mon_tx="$(printf '%s\n' "$metrics" | metric monitor_net_tx_mbps)"
  [[ -n "$mon_mem" ]] || mon_mem=nan
  [[ -n "$mon_disk" ]] || mon_disk=nan
  [[ -n "$mon_rx" ]] || mon_rx=nan
  [[ -n "$mon_tx" ]] || mon_tx=nan

  ref_mem="$(mem_ref)"
  mem_err="$(awk -v a="$ref_mem" -v b="$mon_mem" 'BEGIN {if (a=="nan" || b=="nan") {print "nan"; exit}; d=a-b; if (d<0) d=-d; printf "%.4f", d}')"
  disk_err="$(awk -v a="$ref_disk" -v b="$mon_disk" 'BEGIN {if (a=="nan" || b=="nan") {print "nan"; exit}; d=a-b; if (d<0) d=-d; printf "%.2f", d}')"

  echo "$phase,$i,$ref_mem,$mon_mem,$mem_err,$ref_disk,$mon_disk,$disk_err,$ref_rx,$ref_tx,$mon_rx,$mon_tx" >> "$OUT"
}

run_phase() {
  local phase="$1"
  local samples="$2"
  echo "phase=$phase" >&2
  sleep "$WARMUP_S"
  for i in $(seq 1 "$samples"); do
    sample_once "$phase" "$i"
  done
}

run_phase baseline "$SAMPLES"

python3 - "$MEM_ALLOC_MIB" <<'PY' &
import sys
import time

mib = int(sys.argv[1])
buf = bytearray(mib * 1024 * 1024)
for i in range(0, len(buf), 4096):
    buf[i] = 1
time.sleep(30)
PY
load_pids=("$!")
run_phase "mem${MEM_ALLOC_MIB}m" "$SAMPLES"
cleanup
load_pids=()

(while true; do
  dd if=/dev/zero of="$DISK_DIR/io_test.bin" bs=1M count=256 conv=fdatasync status=none
  rm -f "$DISK_DIR/io_test.bin"
done) &
load_pids=("$!")
run_phase disk_write "$SAMPLES"
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
for phase in dict.fromkeys(row["phase"] for row in rows):
    rs = [r for r in rows if r["phase"] == phase]
    print("==", phase, "samples", len(rs))
    for key in ("mem_abs_error_pct", "ref_mem_used_pct", "monitor_mem_used_pct", "disk_abs_error", "ref_disk_iops", "monitor_disk_iops"):
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
        print(key, "avg", round(statistics.fmean(vals), 4), "median", round(statistics.median(vals), 4), "max", round(max(vals), 4))
PY
