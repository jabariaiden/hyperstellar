#!/bin/bash
# Builds the native C++ module for Linux.
# Downloads and statically links GLFW, so auditwheel passes.

set -e
echo "=== Building hyperstellar native module (Linux) ==="

ORIGINAL_DIR=$(pwd)
BUILD_DIR="_build_linux"
GLFW_VERSION="3.4"

# ------------------------------------------------------------------------
# 1. Environment (optional)
# ------------------------------------------------------------------------
echo "1. Activating environment..."
ENV_SCRIPT="$HOME/hyperstellar_env/bin/activate"
if [ -f "$ENV_SCRIPT" ]; then
    source "$ENV_SCRIPT"
else
    echo "Running without venv (CI mode)"
fi

# ------------------------------------------------------------------------
# 2. Clean
# ------------------------------------------------------------------------
echo "2. Cleaning..."
rm -rf "$BUILD_DIR"

# ------------------------------------------------------------------------
# 3. Copy shaders
# ------------------------------------------------------------------------
echo "3. Copying shaders..."
if [ -d "shaders" ]; then
    rm -rf "python_module/shaders"
    mkdir -p "python_module/shaders"
    cp -r shaders/. python_module/shaders/
    echo "Copied shaders"
else
    echo "ERROR: shaders/ not found"
    exit 1
fi

# ------------------------------------------------------------------------
# 4. Ensure glad.c is in src/
# ------------------------------------------------------------------------
echo "4. Fixing source paths..."
if [ -f "include/glad/glad.c" ]; then
    cp "include/glad/glad.c" "src/"
fi

# ------------------------------------------------------------------------
# 5. Build GLFW as a static library
# ------------------------------------------------------------------------
echo "5. Building GLFW static library..."
wget -q "https://github.com/glfw/glfw/releases/download/${GLFW_VERSION}/glfw-${GLFW_VERSION}.tar.gz"
tar -xzf glfw-${GLFW_VERSION}.tar.gz
mkdir -p glfw-build
cd glfw-build
cmake ../glfw-${GLFW_VERSION} \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DGLFW_BUILD_EXAMPLES=OFF \
    -DGLFW_BUILD_TESTS=OFF \
    -DGLFW_BUILD_DOCS=OFF
make -j$(nproc)
cd ..
GLFW_ROOT="$(pwd)/glfw-build"
GLFW_INCLUDE_DIR="$(pwd)/glfw-${GLFW_VERSION}/include"
GLFW_LIBRARY="${GLFW_ROOT}/src/libglfw3.a"

# ------------------------------------------------------------------------
# 6. Build the C++ module with CMake, passing GLFW paths
# ------------------------------------------------------------------------
echo "6. Building C++ module..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ../python_module \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-DNO_TEXT_RENDERING -DPYTHON_MODULE=1" \
    -DGLFW_LIBRARY="${GLFW_LIBRARY}" \
    -DGLFW_INCLUDE_DIR="${GLFW_INCLUDE_DIR}"
cmake --build . -- -j$(nproc)

# Locate the produced shared library
SO_FILE=$(find . -name "stellar*.so" | head -1)
if [ -z "$SO_FILE" ]; then
    echo "ERROR: No .so produced"
    cd "$ORIGINAL_DIR"
    exit 1
fi

# Copy it and shaders into the Python package source tree
TARGET_DIR="../hyperstellar/src/hyperstellar/_native/linux-x64"
mkdir -p "$TARGET_DIR/shaders"
cp "$SO_FILE" "$TARGET_DIR/stellar.so"
cp -r "../python_module/shaders/." "$TARGET_DIR/shaders/"
echo "Native module built and copied."

cd "$ORIGINAL_DIR"
echo "=== Done ==="