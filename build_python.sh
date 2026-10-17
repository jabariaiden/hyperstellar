#!/usr/bin/env bash
# build_python.sh -- builds ONLY the native module (stellar.so) for the active Python.
# cibuildwheel runs this once per Python version (before-build), then builds the
# wheel itself and repairs it with auditwheel. Do NOT build wheels in here.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
ROOT="$(pwd)"
GLFW_VERSION="3.4"
BUILD_DIR="$ROOT/_build_linux"
DEPS_DIR="$ROOT/_deps_linux"      # survives between the per-Python builds (GLFW is built once)

# Optional local venv 
ENV_SCRIPT="$HOME/hyperstellar_env/bin/activate"
if [ -z "${VIRTUAL_ENV:-}" ] && [ -f "$ENV_SCRIPT" ]; then source "$ENV_SCRIPT"; fi

PYTHON="$(command -v python || command -v python3)"
echo "=== Building hyperstellar native module (Linux) with $($PYTHON --version 2>&1) ==="

echo "1. Cleaning stale per-Python artifacts..."
# build/ and egg-info from a previous Python version must not leak into this wheel
rm -rf "$BUILD_DIR" "$ROOT/build" "$ROOT"/*.egg-info "$ROOT"/hyperstellar/src/*.egg-info
rm -f  "$ROOT/hyperstellar/src/hyperstellar/_native/linux-x64/stellar.so"

echo "2. Copying shaders..."
[ -d shaders ] || { echo "ERROR: shaders/ not found"; exit 1; }
rm -rf python_module/shaders
mkdir -p python_module/shaders
cp -r shaders/. python_module/shaders/

echo "3. glad.c..."
if [ -f include/glad/glad.c ]; then cp include/glad/glad.c src/; fi
[ -f src/glad.c ] || echo "WARNING: src/glad.c not found - build may fail"

echo "4. GLFW ${GLFW_VERSION} (static, PIC) ..."
mkdir -p "$DEPS_DIR"
GLFW_SRC="$DEPS_DIR/glfw-${GLFW_VERSION}"
GLFW_BLD="$DEPS_DIR/glfw-build"
GLFW_LIBRARY="$GLFW_BLD/src/libglfw3.a"
GLFW_INCLUDE_DIR="$GLFW_SRC/include"
if [ ! -f "$GLFW_LIBRARY" ]; then
    rm -rf "$GLFW_SRC" "$GLFW_BLD"
    # NOTE: releases/download/3.4/glfw-3.4.tar.gz does NOT exist (404). The
    # auto-generated tag archive does, and extracts to glfw-3.4/.
    curl -fsSL --retry 5 --retry-delay 3 \
        -o "$DEPS_DIR/glfw.tar.gz" \
        "https://github.com/glfw/glfw/archive/refs/tags/${GLFW_VERSION}.tar.gz"
    tar -xzf "$DEPS_DIR/glfw.tar.gz" -C "$DEPS_DIR"
    cmake -S "$GLFW_SRC" -B "$GLFW_BLD" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_SHARED_LIBS=OFF \
        -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
        -DGLFW_BUILD_WAYLAND=OFF -DGLFW_BUILD_X11=ON
    cmake --build "$GLFW_BLD" -j"$(nproc)"
fi
[ -f "$GLFW_LIBRARY" ] || { echo "ERROR: $GLFW_LIBRARY not produced"; exit 1; }

echo "5. pybind11 (pinned via pip; supports CPython 3.14)..."
"$PYTHON" -m pip install --quiet "pybind11>=3.0.1,<4"
PYBIND11_DIR="$("$PYTHON" -m pybind11 --cmakedir)"

echo "6. Building C++ module..."
mkdir -p "$BUILD_DIR"
cmake -S "$ROOT/python_module" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_CXX_FLAGS="-DNO_TEXT_RENDERING -DPYTHON_MODULE=1" \
    -DPython3_EXECUTABLE="$PYTHON" \
    -Dpybind11_DIR="$PYBIND11_DIR" \
    -DGLFW_LIBRARY="$GLFW_LIBRARY" \
    -DGLFW_INCLUDE_DIR="$GLFW_INCLUDE_DIR"
cmake --build "$BUILD_DIR" -j"$(nproc)"

SO_FILE="$(find "$BUILD_DIR" -name 'stellar*.so' | head -1)"
[ -n "$SO_FILE" ] || { echo "ERROR: no stellar .so produced"; exit 1; }

TARGET_DIR="$ROOT/hyperstellar/src/hyperstellar/_native/linux-x64"
mkdir -p "$TARGET_DIR/shaders"
cp "$SO_FILE" "$TARGET_DIR/stellar.so"
cp -r "$ROOT/python_module/shaders/." "$TARGET_DIR/shaders/"

if command -v nm >/dev/null 2>&1; then
    nm -D --defined-only "$TARGET_DIR/stellar.so" | grep -q PyInit_stellar \
        || { echo "ERROR: stellar.so does not export PyInit_stellar"; exit 1; }
fi
echo "   needed libs:"; (readelf -d "$TARGET_DIR/stellar.so" | grep NEEDED) || true
echo "=== Native module built for $($PYTHON --version 2>&1) ==="