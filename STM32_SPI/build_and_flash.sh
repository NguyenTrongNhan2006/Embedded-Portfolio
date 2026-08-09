#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

BUILD_DIR="build"

rm -rf "$BUILD_DIR"

cmake -B "$BUILD_DIR" -G Ninja
cmake --build "$BUILD_DIR"

BIN_FILE="$(find "$BUILD_DIR" -maxdepth 1 -type f -name "*.bin" | head -n 1)"

if [[ -z "$BIN_FILE" ]]; then
    echo "ERROR: BIN file not found."
    exit 1
fi

STM32_Programmer_CLI \
    -c port=SWD \
    -w "$BIN_FILE" 0x08000000 \
    -v \
    -rst

echo "BUILD AND FLASH SUCCESSFUL"