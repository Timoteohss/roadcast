#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="$ROOT/build/android-arm64-v8a-api28/roadcastd"
REMOTE_PATH="/data/local/tmp/roadcastd"
ADB_SERIAL="${ADB_SERIAL:-}"
START_AFTER_INSTALL=1

usage() {
  cat <<EOF
Usage: scripts/install-android.sh [options]

Installs Roadcast at the same path used by the app-managed deployment.

Options:
  --device SERIAL  Use a specific adb device.
  --binary PATH    Install a specific ARM64 Roadcast executable.
  --no-start       Install without starting the daemon.
  -h, --help       Show this help.

This is a development and recovery tool. Production distribution and
supervision are owned by the privileged GeelyBattery app.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --device)
      ADB_SERIAL="${2:?Missing serial after --device}"
      shift 2
      ;;
    --binary)
      BINARY="${2:?Missing path after --binary}"
      shift 2
      ;;
    --no-start)
      START_AFTER_INSTALL=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

adb_cmd() {
  if [[ -n "$ADB_SERIAL" ]]; then
    adb -s "$ADB_SERIAL" "$@"
  else
    adb "$@"
  fi
}

[[ -f "$BINARY" ]] || {
  echo "Roadcast Android executable not found: $BINARY" >&2
  echo "Run scripts/build-android.sh first or pass --binary." >&2
  exit 1
}

echo "Using Roadcast executable: $BINARY"
adb_cmd root
adb_cmd wait-for-device

echo "Stopping an existing Roadcast process"
adb_cmd shell 'for pid in $(pidof roadcastd 2>/dev/null); do kill "$pid"; done'
for _ in $(seq 1 60); do
  if [[ -z "$(adb_cmd shell 'pidof roadcastd 2>/dev/null' | tr -d '\r')" ]]; then
    break
  fi
  sleep 0.05
done
if [[ -n "$(adb_cmd shell 'pidof roadcastd 2>/dev/null' | tr -d '\r')" ]]; then
  echo "Roadcast did not stop before installation" >&2
  exit 1
fi

echo "Installing $REMOTE_PATH"
adb_cmd push "$BINARY" "$REMOTE_PATH"
adb_cmd shell "chown root:root '$REMOTE_PATH'"
adb_cmd shell "chmod 0755 '$REMOTE_PATH'"

LOCAL_HASH="$(shasum -a 256 "$BINARY" | awk '{print $1}')"
REMOTE_HASH="$(adb_cmd shell "sha256sum '$REMOTE_PATH'" | awk '{print $1}' | tr -d '\r')"
if [[ "$LOCAL_HASH" != "$REMOTE_HASH" ]]; then
  echo "Installed Roadcast checksum mismatch" >&2
  exit 1
fi

adb_cmd shell "ls -lZ '$REMOTE_PATH'"
echo "Installed sha256: $REMOTE_HASH"

if [[ "$START_AFTER_INSTALL" -eq 1 ]]; then
  echo "Starting Roadcast through root adbd"
  adb_cmd shell \
    "nohup '$REMOTE_PATH' --hz 60 --socket @roadcast </dev/null >/dev/null 2>&1 &"
  sleep 1
  adb_cmd shell "ps -AZ | grep '[r]oadcastd' || true"
fi
