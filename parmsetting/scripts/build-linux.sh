#!/usr/bin/env bash
# =============================================================================
# build-linux.sh —— Linux (GCC) 一键构建
#
#   依赖（Debian/Ubuntu）：
#     sudo apt install build-essential cmake ninja-build git \
#                      libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev \
#                      libxcursor-dev libxi-dev
#   （要让 GLFW 同时支持 Wayland，再加 libwayland-dev libxkbcommon-dev wayland-protocols，
#     并给 cmake 加 -DPARM_UI_WAYLAND=ON）
#
#   用法：
#     bash scripts/build-linux.sh                 # Release
#     bash scripts/build-linux.sh Debug           # Debug
# =============================================================================
set -euo pipefail

CONFIG="${1:-Release}"
# 一律用相对路径（相对仓库根目录）
BUILD_DIR="build/gcc"

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cmake -S . -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG"
cmake --build "$BUILD_DIR"
echo "[build] 完成，产物在: $BUILD_DIR/bin"
