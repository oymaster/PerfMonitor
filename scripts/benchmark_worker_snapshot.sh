#!/usr/bin/env bash
set -euo pipefail

MANAGER_HTTP="${MANAGER_HTTP:-http://127.0.0.1:8080}"
DURATION="${DURATION:-300}"
INTERVAL="${INTERVAL:-5}"
OUT_DIR="${OUT_DIR:-/tmp/perfmonitor-worker-benchmark-$(date +%Y%m%d-%H%M%S)}"
WORKER_PID="${WORKER_PID:-}"

mkdir -p "$OUT_DIR"

if [ -z "$WORKER_PID" ]; then
  WORKER_PID="$(pgrep -f '(^|/)worker($| )' | head -1 || true)"
fi

if [ -z "$WORKER_PID" ]; then
  echo "WORKER_PID is required when the worker process cannot be found automatically" >&2
  exit 2
fi

echo "timestamp,pid,cpu_pct,rss_kb,vsz_kb,metrics_http_code,metrics_time_s,events_http_code,events_time_s,event_count" \
  >"$OUT_DIR/worker_snapshot.csv"

{
  echo "date=$(date -Iseconds)"
  echo "hostname=$(hostname)"
  echo "uname=$(uname -a)"
  echo "worker_pid=$WORKER_PID"
  command -v lscpu >/dev/null 2>&1 && lscpu || true
  command -v free >/dev/null 2>&1 && free -h || true
} >"$OUT_DIR/host_info.txt"

deadline=$((SECONDS + DURATION))
while [ "$SECONDS" -lt "$deadline" ]; do
  ts="$(date -Iseconds)"
  ps_line="$(ps -p "$WORKER_PID" -o pid=,pcpu=,rss=,vsz= 2>/dev/null || true)"
  if [ -z "$ps_line" ]; then
    echo "worker process exited: $WORKER_PID" >&2
    break
  fi
  read -r pid cpu rss vsz <<<"$ps_line"

  metrics_tmp="$OUT_DIR/metrics.tmp"
  events_tmp="$OUT_DIR/events.tmp"
  metrics_stats="$(curl -sS -o "$metrics_tmp" -w '%{http_code},%{time_total}' "$MANAGER_HTTP/metrics" || echo '000,0')"
  events_stats="$(curl -sS -o "$events_tmp" -w '%{http_code},%{time_total}' "$MANAGER_HTTP/api/events" || echo '000,0')"
  event_count="$(grep -o '"type"' "$events_tmp" 2>/dev/null | wc -l | tr -d ' ')"

  echo "$ts,$pid,$cpu,$rss,$vsz,$metrics_stats,$events_stats,$event_count" \
    >>"$OUT_DIR/worker_snapshot.csv"
  sleep "$INTERVAL"
done

cp "$metrics_tmp" "$OUT_DIR/last_metrics.txt" 2>/dev/null || true
cp "$events_tmp" "$OUT_DIR/last_events.json" 2>/dev/null || true
echo "output_dir=$OUT_DIR"
