#!/bin/bash
# build_python.sh - Builds the native C++ module for Linux.
# It copies shaders, prepares sources, runs CMake, and places the final
# stellar.so and shaders into the Python package source tree.
# This script does NOT build the wheel; that is handled by cibuildwheel.

set -e

echo "=== Building hyperstellar native module (Linux) ==="

# ------------------------------------------------------------------------
# 1. Activate environment (optional, skipped in CI)
# ------------------------------------------------------------------------
echo "1. Activating Python environment..."
ENV_SCRIPT="$HOME/hyperstellar_env/bin/activate"
if [ -f "$ENV_SCRIPT" ]; then
    source "$ENV_SCRIPT"
    echo "   ✓ Activated hyperstellar_env"
else
    echo "   ✓ Running without venv (CI mode)"
fi

ORIGINAL_DIR=$(pwd)
BUILD_DIR="_build_linux"
echo "   Working from: $ORIGINAL_DIR"

# ------------------------------------------------------------------------
# 2. Clean
# ------------------------------------------------------------------------
echo "2. Cleaning..."
rm -rf "$BUILD_DIR"
echo "   ✓ Cleaned"

# ------------------------------------------------------------------------
# 3. Copy shaders into python_module/shaders (CMake will copy them later)
# ------------------------------------------------------------------------
echo "3. Copying shaders..."
if [ -d "shaders" ]; then
    rm -rf "python_module/shaders"
    mkdir -p "python_module/shaders"
    cp -r shaders/. python_module/shaders/
    COUNT=$(find shaders -type f | wc -l)
    echo "   ✓ Copied $COUNT shader files"
else
    echo "   ERROR: shaders/ directory not found"
    exit 1
fi

# ------------------------------------------------------------------------
# 4. Ensure glad.c is in src/
# ------------------------------------------------------------------------
echo "4. Fixing source paths..."
if [ -f "include/glad/glad.c" ]; then
    cp "include/glad/glad.c" "src/"
    echo "   ✓ Copied glad.c to src/"
elif [ -f "src/glad.c" ]; then
    echo "   ✓ glad.c already in src/"
else
    echo "   WARNING: glad.c not found — build may fail"
fi

# ------------------------------------------------------------------------
# 5. Build the native module with CMake
# ------------------------------------------------------------------------
echo "5. Building C++ module..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "   Configuring..."
cmake ../python_module \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-DNO_TEXT_RENDERING -DPYTHON_MODULE=1"

echo "   Compiling ($(nproc) cores)..."
cmake --build . -- -j$(nproc)

# Locate the produced shared library
SO_FILE=$(find . -name "stellar*.so" | head -1)
if [ -z "$SO_FILE" ]; then
    echo "   ERROR: No .so produced"
    cd "$ORIGINAL_DIR"
    exit 1
fi

# Copy it and the shaders into the Python package source tree
TARGET_DIR="../hyperstellar/src/hyperstellar/_native/linux-x64"
mkdir -p "$TARGET_DIR/shaders"
cp "$SO_FILE" "$TARGET_DIR/stellar.so"
cp -r "../python_module/shaders/." "$TARGET_DIR/shaders/"
echo "   ✓ $(basename $SO_FILE) → linux-x64/stellar.so"
echo "   ✓ Shaders copied"

cd "$ORIGINAL_DIR"
echo "=== Native module built successfully ==="