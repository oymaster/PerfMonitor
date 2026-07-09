#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
MANAGER_BIN="${MANAGER_BIN:-$BUILD_DIR/manager/manager}"
BENCH_BIN="${BENCH_BIN:-$BUILD_DIR/benchmarks/manager_fake_worker}"
WORKERS="${WORKERS:-100}"
ITERATIONS="${ITERATIONS:-10}"
CONCURRENCY="${CONCURRENCY:-8}"
INTERVAL_MS="${INTERVAL_MS:-0}"

if [ ! -x "$BENCH_BIN" ]; then
  echo "missing benchmark binary: $BENCH_BIN" >&2
  echo "build with: cmake -S . -B build -DBUILD_BENCHMARKS=ON && cmake --build build -j" >&2
  exit 2
fi

PIDS=""
cleanup() {
  for pid in $PIDS; do
    kill "$pid" >/dev/null 2>&1 || true
    wait "$pid" >/dev/null 2>&1 || true
  done
}
trap cleanup EXIT INT TERM

if ! nc -z 127.0.0.1 50051 >/dev/null 2>&1; then
  if [ ! -x "$MANAGER_BIN" ]; then
    echo "manager is not running and manager binary is missing: $MANAGER_BIN" >&2
    exit 2
  fi
  "$MANAGER_BIN" >/tmp/perfmonitor-manager-benchmark.log 2>&1 &
  PIDS="$! $PIDS"
  for _ in $(seq 1 100); do
    nc -z 127.0.0.1 50051 >/dev/null 2>&1 && break
    sleep 0.1
  done
fi

"$BENCH_BIN" \
  --addr 127.0.0.1:50051 \
  --workers "$WORKERS" \
  --iterations "$ITERATIONS" \
  --concurrency "$CONCURRENCY" \
  --interval-ms "$INTERVAL_MS"
