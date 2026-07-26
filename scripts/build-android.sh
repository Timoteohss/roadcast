#!/usr/bin/env bash
set -euo pipefail

: "${ANDROID_NDK:?set ANDROID_NDK to an installed Android NDK}"

API="${API:-28}"
ABI="${ABI:-arm64-v8a}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOT/build/android-$ABI-api$API"
DEFAULT_LIBUV_PREFIX="$ROOT/.deps/libuv-android-$ABI-api$API"
LIBUV_PREFIX="${LIBUV_PREFIX:-$DEFAULT_LIBUV_PREFIX}"

"$ROOT/scripts/generate-catalog.py"

case "$(uname -s)" in
  Darwin) HOST_TAG="darwin-x86_64" ;;
  Linux) HOST_TAG="linux-x86_64" ;;
  *)
    echo "Unsupported build host: $(uname -s)" >&2
    exit 1
    ;;
esac

TOOLCHAIN="$ANDROID_NDK/toolchains/llvm/prebuilt/$HOST_TAG/bin"
CC="$TOOLCHAIN/aarch64-linux-android${API}-clang"
if [ ! -x "$CC" ]; then
  echo "Android compiler not found: $CC" >&2
  exit 1
fi
if [ ! -f "$LIBUV_PREFIX/include/uv.h" ] ||
  [ ! -f "$LIBUV_PREFIX/lib/libuv.a" ]; then
  echo "Android libuv not found at $LIBUV_PREFIX" >&2
  echo "Run scripts/build-libuv-android.sh or set LIBUV_PREFIX." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

COMMON_FLAGS=(
  -std=c11
  -O2
  -g
  -Wall
  -Wextra
  -Wpedantic
  -Werror
  -I"$ROOT/include"
)

"$CC" "${COMMON_FLAGS[@]}" -fPIE -I"$LIBUV_PREFIX/include" \
  -o "$OUT_DIR/roadcastd" \
  "$ROOT/src/roadcastd.c" \
  "$ROOT/src/vhal_source.c" \
  "$ROOT/src/protocol.c" \
  "$ROOT/src/catalog.c" \
  "$ROOT/src/catalog_generated.c" \
  "$LIBUV_PREFIX/lib/libuv.a" \
  -pie -pthread -ldl

"$CC" "${COMMON_FLAGS[@]}" -fPIE \
  -o "$OUT_DIR/roadcastctl" \
  "$ROOT/src/roadcastctl.c" \
  "$ROOT/src/protocol.c" \
  -pie

"$CC" "${COMMON_FLAGS[@]}" -fPIC -shared \
  -o "$OUT_DIR/libroadcast_client.so" \
  "$ROOT/src/client.c" \
  "$ROOT/src/protocol.c" \
  -pthread

echo "Android binaries:"
file "$OUT_DIR/roadcastd" "$OUT_DIR/roadcastctl" \
  "$OUT_DIR/libroadcast_client.so" 2>/dev/null || true
