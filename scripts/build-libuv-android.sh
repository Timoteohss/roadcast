#!/usr/bin/env bash
set -euo pipefail

: "${ANDROID_NDK:?set ANDROID_NDK to an installed Android NDK}"

LIBUV_VERSION="${LIBUV_VERSION:-1.51.0}"
API="${API:-28}"
ABI="${ABI:-arm64-v8a}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEPS_DIR="$ROOT/.deps"
SOURCE_DIR="$DEPS_DIR/libuv-$LIBUV_VERSION"
BUILD_DIR="$DEPS_DIR/libuv-build-$ABI-api$API"
PREFIX="${LIBUV_PREFIX:-$DEPS_DIR/libuv-android-$ABI-api$API}"
ARCHIVE="$DEPS_DIR/libuv-v$LIBUV_VERSION.tar.gz"
URL="https://github.com/libuv/libuv/archive/refs/tags/v$LIBUV_VERSION.tar.gz"

mkdir -p "$DEPS_DIR"

if [ ! -d "$SOURCE_DIR" ]; then
  if [ ! -f "$ARCHIVE" ]; then
    curl --fail --location "$URL" --output "$ARCHIVE"
  fi
  rm -rf "$SOURCE_DIR"
  tar -xzf "$ARCHIVE" -C "$DEPS_DIR"
fi

cmake -S "$SOURCE_DIR" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" \
  -DANDROID_PLATFORM="android-$API" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DBUILD_TESTING=OFF \
  -DBUILD_SHARED_LIBS=OFF

cmake --build "$BUILD_DIR" --target install --parallel

echo "Android libuv installed at $PREFIX"
