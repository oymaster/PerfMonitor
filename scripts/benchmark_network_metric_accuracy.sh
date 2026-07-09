#!/usr/bin/env bash
set -u

OUT_DIR="${OUT_DIR:-benchmarks/network_metric_accuracy_$(date +%Y%m%d_%H%M%S)}"
MANAGER_HTTP="${MANAGER_HTTP:-http://127.0.0.1:8080}"
SAMPLES="${SAMPLES:-40}"

mkdir -p "$OUT_DIR"
OUT="$OUT_DIR/network_accuracy.csv"

echo 'phase,sample,ref_net_rx_mbps,ref_net_tx_mbps,monitor_net_rx_mbps,monitor_net_tx_mbps,rx_abs_error_mbps,tx_abs_error_mbps' > "$OUT"

metric() {
  local name="$1"
  awk -v n="$name" '$1 ~ ("^" n) {print $2; exit}'
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

for i in $(seq 1 "$SAMPLES"); do
  read -r rx0 tx0 <<<"$(net_raw)"
  sleep 1
  read -r rx1 tx1 <<<"$(net_raw)"
  ref_rx="$(awk -v a="$rx0" -v b="$rx1" 'BEGIN {printf "%.4f", (b-a)*8.0/1024/1024}')"
  ref_tx="$(awk -v a="$tx0" -v b="$tx1" 'BEGIN {printf "%.4f", (b-a)*8.0/1024/1024}')"

  metrics="$(curl -sf --max-time 3 "$MANAGER_HTTP/metrics" || true)"
  mon_rx="$(printf '%s\n' "$metrics" | metric monitor_net_rx_mbps)"
  mon_tx="$(printf '%s\n' "$metrics" | metric monitor_net_tx_mbps)"
  [[ -n "$mon_rx" ]] || mon_rx=nan
  [[ -n "$mon_tx" ]] || mon_tx=nan

  rx_err="$(awk -v a="$ref_rx" -v b="$mon_rx" 'BEGIN {if (a=="nan" || b=="nan") {print "nan"; exit}; d=a-b; if (d<0) d=-d; printf "%.4f", d}')"
  tx_err="$(awk -v a="$ref_tx" -v b="$mon_tx" 'BEGIN {if (a=="nan" || b=="nan") {print "nan"; exit}; d=a-b; if (d<0) d=-d; printf "%.4f", d}')"
  echo "external_transfer,$i,$ref_rx,$ref_tx,$mon_rx,$mon_tx,$rx_err,$tx_err" >> "$OUT"
done

python3 - "$OUT" <<'PY'
import csv
import math
import pathlib
import statistics
import sys

path = pathlib.Path(sys.argv[1])
rows = list(csv.DictReader(path.open()))
print(f"csv={path}")
active = [r for r in rows if float(r["ref_net_tx_mbps"]) > 10.0 or float(r["ref_net_rx_mbps"]) > 10.0]
for label, subset in (("all", rows), ("active_gt_10_mbps", active)):
    print("==", label, "samples", len(subset))
    for key in ("ref_net_rx_mbps", "ref_net_tx_mbps", "monitor_net_rx_mbps", "monitor_net_tx_mbps", "rx_abs_error_mbps", "tx_abs_error_mbps"):
        vals = []
        for row in subset:
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
