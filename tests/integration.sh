#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/roadcast-test.XXXXXX")"
SOCKET="$TEMP_DIR/roadcast.sock"
DAEMON_PID=""

cleanup() {
  if [ -n "$DAEMON_PID" ] && kill -0 "$DAEMON_PID" 2>/dev/null; then
    kill -TERM "$DAEMON_PID"
    wait "$DAEMON_PID" || true
  fi
  rm -rf "$TEMP_DIR"
}
trap cleanup EXIT INT TERM

"$ROOT/build/roadcastd" --fake --hz 240 --socket "$SOCKET" \
  >"$TEMP_DIR/daemon.stdout" 2>"$TEMP_DIR/daemon.stderr" &
DAEMON_PID=$!

for _ in $(seq 1 100); do
  [ -S "$SOCKET" ] && break
  kill -0 "$DAEMON_PID"
  sleep 0.02
done
[ -S "$SOCKET" ]

"$ROOT/build/roadcastctl" --socket "$SOCKET" --seconds 2 \
  --signal VehicleSpeed \
  >"$TEMP_DIR/client-a.stdout" 2>"$TEMP_DIR/client-a.stderr" &
CLIENT_A_PID=$!
"$ROOT/build/roadcastctl" --socket "$SOCKET" --seconds 2 \
  >"$TEMP_DIR/client-b.stdout" 2>"$TEMP_DIR/client-b.stderr" &
CLIENT_B_PID=$!
"$ROOT/build/roadcastctl" --socket "$SOCKET" --stall-after-subscribe 4 \
  >"$TEMP_DIR/slow-client.stdout" 2>"$TEMP_DIR/slow-client.stderr" &
SLOW_CLIENT_PID=$!

wait "$CLIENT_A_PID"
wait "$CLIENT_B_PID"
wait "$SLOW_CLIENT_PID"

for client in client-a client-b; do
  grep -q '^welcome: protocol=2 hz=240 frames=111 signals=815 ' \
    "$TEMP_DIR/$client.stdout"
  grep -q '^schema: version=1 signals=815 ' "$TEMP_DIR/$client.stdout"
  grep -q '^snapshot: frames=111 signals=815 ' "$TEMP_DIR/$client.stdout"
  grep -q '^stream: frame_batches=' "$TEMP_DIR/$client.stdout"
  if grep -Eq 'gaps=[1-9][0-9]*' "$TEMP_DIR/$client.stdout"; then
    echo "$client observed a sequence gap" >&2
    exit 1
  fi
done

grep -q '^schema-entry: .*name=ESC_VehicleSpeed ' \
  "$TEMP_DIR/client-a.stdout"
grep -q '^signal: .*name=ESC_VehicleSpeed ' "$TEMP_DIR/client-a.stdout"
grep -q '^update: .*name=ESC_VehicleSpeed ' "$TEMP_DIR/client-a.stdout"
grep -q '^stalling: seconds=4$' "$TEMP_DIR/slow-client.stdout"
grep -q 'client queue overflow; resync required' "$TEMP_DIR/daemon.stderr"

kill -TERM "$DAEMON_PID"
wait "$DAEMON_PID"
DAEMON_PID=""

echo "integration tests passed"
