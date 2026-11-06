#!/bin/sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT_DIR"
"$ROOT_DIR/scripts/build.sh"
"$ROOT_DIR/build/sudoku-studio" --self-test

SERVER_LOG="$ROOT_DIR/build/test-server.log"
RESPONSE_FILE="$ROOT_DIR/build/test-response.json"
PAGE_FILE="$ROOT_DIR/build/test-page.html"
STATUS_FILE="$ROOT_DIR/build/static-status.txt"
"$ROOT_DIR/build/sudoku-studio" 8080 "$ROOT_DIR/dist" >"$SERVER_LOG" 2>&1 &
SERVER_PID=$!
cleanup() {
  kill "$SERVER_PID" 2>/dev/null || true
  wait "$SERVER_PID" 2>/dev/null || true
  rm -f "$RESPONSE_FILE" "$PAGE_FILE" "$STATUS_FILE"
}
trap cleanup EXIT HUP INT TERM

request() {
  REQUEST_METHOD=$1
  REQUEST_PATH=$2
  REQUEST_BODY=$3
  REQUEST_EXPECTED_STATUS=$4
  REQUEST_EXPECTED_TEXT=$5
  if [ "$REQUEST_METHOD" = GET ]; then
    REQUEST_STATUS=$(curl -sS --max-time 12 -o "$RESPONSE_FILE" -w '%{http_code}' "http://127.0.0.1:8080$REQUEST_PATH")
  else
    REQUEST_STATUS=$(curl -sS --max-time 12 -o "$RESPONSE_FILE" -w '%{http_code}' \
      -X "$REQUEST_METHOD" -H 'Content-Type: text/plain' --data-binary "$REQUEST_BODY" \
      "http://127.0.0.1:8080$REQUEST_PATH")
  fi
  if [ "$REQUEST_STATUS" != "$REQUEST_EXPECTED_STATUS" ]; then
    echo "FAIL $REQUEST_METHOD $REQUEST_PATH: expected HTTP $REQUEST_EXPECTED_STATUS, received $REQUEST_STATUS"
    cat "$RESPONSE_FILE"
    exit 1
  fi
  if [ -n "$REQUEST_EXPECTED_TEXT" ] && ! grep -Fq "$REQUEST_EXPECTED_TEXT" "$RESPONSE_FILE"; then
    echo "FAIL $REQUEST_METHOD $REQUEST_PATH: response did not include $REQUEST_EXPECTED_TEXT"
    cat "$RESPONSE_FILE"
    exit 1
  fi
  echo "PASS $REQUEST_METHOD $REQUEST_PATH ($REQUEST_STATUS)"
}

attempt=0
until request GET /api/health '' 200 '"status":"ok"'; do
  attempt=$((attempt + 1))
  if [ "$attempt" -ge 20 ]; then echo 'CivetWeb did not become ready.'; cat "$SERVER_LOG"; exit 1; fi
  sleep 0.25
done

curl -sS --max-time 12 -o "$PAGE_FILE" -w '%{http_code}' http://127.0.0.1:8080/ > "$ROOT_DIR/build/static-status.txt"
if [ "$(cat "$ROOT_DIR/build/static-status.txt")" != 200 ] || ! grep -Fq 'Sudoku Studio' "$PAGE_FILE"; then
  echo 'FAIL static frontend delivery'; exit 1
fi
echo 'PASS static frontend delivery'
JS_ASSET=$(sed -n 's/.*src="\([^"]*\.js\)".*/\1/p' "$PAGE_FILE" | head -1)
if [ -z "$JS_ASSET" ] || [ "$(curl -sS -o /dev/null -w '%{http_code}' "http://127.0.0.1:8080$JS_ASSET")" != 200 ]; then
  echo 'FAIL built JavaScript asset delivery'; exit 1
fi
echo 'PASS built JavaScript asset delivery'

EASY='530070000600195000098000060800060003400803001700020006060000280000419005000080079'
SOLUTION='534678912672195348198342567859761423426853791713924856961537284287419635345286179'
ZEROS='000000000000000000000000000000000000000000000000000000000000000000000000000000000'
request GET '/api/puzzle?difficulty=easy' '' 200 '"puzzle"'
request GET '/api/puzzle?difficulty=medium' '' 200 '"puzzle"'
request GET '/api/puzzle?difficulty=hard' '' 200 '"puzzle"'
request POST /api/validate "$EASY" 200 '"status":"valid"'
request POST /api/validate "$ZEROS" 200 '"status":"valid"'
request POST /api/solve "$EASY" 200 '"status":"solved"'
request POST /api/solve "${EASY%?}x" 400 '"status":"invalid_input"'
request POST /api/solve "${EASY%?}" 400 '"status":"invalid_input"'
request POST /api/solve '110000000000000000000000000000000000000000000000000000000000000000000000000000000' 422 '"status":"invalid_input"'
request POST /api/validate '100000000100000000000000000000000000000000000000000000000000000000000000000000000' 422 '"status":"invalid_input"'
request POST /api/validate '100000000010000000000000000000000000000000000000000000000000000000000000000000000' 422 '"status":"invalid_input"'
request POST /api/solve "$("$ROOT_DIR/build/sudoku-studio" --print-unsolvable-fixture)" 422 '"status":"unsolvable"'
request POST /api/check "$EASY$ZEROS" 200 '"status":"incomplete"'
request POST /api/check "$EASY$SOLUTION" 200 '"status":"correct"'
WRONG_SOLUTION="1${SOLUTION#?}"
request POST /api/check "$EASY$WRONG_SOLUTION" 200 '"status":"incorrect"'
MODIFIED_SOLUTION="4${SOLUTION#?}"
request POST /api/check "$EASY$MODIFIED_SOLUTION" 200 '"status":"incorrect"'
request POST /api/check '000000000000000000000000000000000000000000000000000000000000000000000000000000000' 400 '"status":"invalid_input"'
request POST /api/generate random 200 '"unique_solution":true'
request POST /api/generate impossible 400 '"status":"error"'

echo 'All C engine and API smoke checks passed.'
