#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 3 ]]; then
  echo "Usage: $0 <tei-xml> <gguf-model> <out-dir> [workers_csv]"
  exit 1
fi

INPUT_XML="$1"
MODEL="$2"
OUT_DIR="$3"
WORKERS_CSV="${4:-1,2,4,8,12}"

IFS=',' read -r -a WORKERS <<<"$WORKERS_CSV"

mkdir -p "$OUT_DIR"

BIN="$(cd "$(dirname "$0")/.." && pwd)/build/tei_mt"
if [[ ! -x "$BIN" ]]; then
  echo "Missing binary: $BIN"
  echo "Build first: cmake -S . -B build && cmake --build build -j"
  exit 1
fi

printf "workers,time_ms,segments,ms_per_segment,seg_per_sec\n"
for w in "${WORKERS[@]}"; do
  RUN_OUT_DIR="$OUT_DIR/w${w}"
  mkdir -p "$RUN_OUT_DIR"

  LOG="$("$BIN" \
    --input "$INPUT_XML" \
    --output "$RUN_OUT_DIR" \
    --model "$MODEL" \
    --workers "$w" \
    2>&1 | tee "$RUN_OUT_DIR/run.log")"

  OK_LINE="$(echo "$LOG" | rg '^\[ok\]' | tail -n1 || true)"
  if [[ -z "$OK_LINE" ]]; then
    printf "%s,ERROR,ERROR,ERROR,ERROR\n" "$w"
    continue
  fi

  TIME_MS="$(echo "$OK_LINE" | sed -n 's/.*time_ms=\([^ ]*\).*/\1/p')"
  SEGMENTS="$(echo "$OK_LINE" | sed -n 's/.*segments=\([^ ]*\).*/\1/p')"
  MS_PER_SEG="$(echo "$OK_LINE" | sed -n 's/.*ms_per_segment=\([^ ]*\).*/\1/p')"
  SEG_PER_SEC="$(echo "$OK_LINE" | sed -n 's/.*seg_per_sec=\([^ ]*\).*/\1/p')"

  printf "%s,%s,%s,%s,%s\n" "$w" "$TIME_MS" "$SEGMENTS" "$MS_PER_SEG" "$SEG_PER_SEC"
done
