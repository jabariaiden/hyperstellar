#!/bin/bash
set -e
echo "=== Building hyperstellar native module (Linux) ==="

ORIGINAL_DIR=$(pwd)
BUILD_DIR="_build_linux"
GLFW_VERSION="3.4"

# Clean build directory and Windows binaries
rm -rf "$BUILD_DIR"
rm -rf "hyperstellar/src/hyperstellar/_native/windows-x64"

echo "1. Preparing source files..."
if [ -f "include/glad/glad.c" ]; then cp "include/glad/glad.c" "src/"; fi

echo "2. Building GLFW static library..."
if [ ! -f "glfw-build/src/libglfw3.a" ]; then
    # FIXED: Correct GitHub archive URL for the source code and explicitly output to a named file
    wget -q "https://github.com/glfw/glfw/archive/refs/tags/${GLFW_VERSION}.tar.gz" -O glfw-${GLFW_VERSION}.tar.gz
    tar -xzf glfw-${GLFW_VERSION}.tar.gz
    mkdir -p glfw-build
    cd glfw-build
    cmake ../glfw-${GLFW_VERSION} \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=OFF \
        -DGLFW_BUILD_EXAMPLES=OFF \
        -DGLFW_BUILD_TESTS=OFF \
        -DGLFW_BUILD_DOCS=OFF \
        -DGLFW_BUILD_WAYLAND=OFF \
        -DGLFW_BUILD_OSMESA=OFF \
        -DGLFW_BUILD_X11=ON
    make -j$(nproc)
    cd ..
fi

GLFW_ROOT="$(pwd)/glfw-build"
GLFW_INCLUDE_DIR="$(pwd)/glfw-${GLFW_VERSION}/include"
GLFW_LIBRARY="${GLFW_ROOT}/src/libglfw3.a"

CURRENT_PYTHON=$(python -c "import sys; print(sys.executable)")
echo "3. Building C++ extension for active Python: ${CURRENT_PYTHON}..."

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake ../python_module \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-DNO_TEXT_RENDERING -DPYTHON_MODULE=1" \
    -DPython3_EXECUTABLE="${CURRENT_PYTHON}" \
    -DGLFW_LIBRARY="${GLFW_LIBRARY}" \
    -DGLFW_INCLUDE_DIR="${GLFW_INCLUDE_DIR}"

# Build CMake (POST_BUILD automatically copies the .so and shaders)
cmake --build . -- -j$(nproc)

cd "$ORIGINAL_DIR"

echo "4. Verifying CMake POST_BUILD copied files..."
TARGET_FILE="hyperstellar/src/hyperstellar/_native/linux-x64/stellar.so"
if [ ! -f "$TARGET_FILE" ]; then
    echo "ERROR: stellar.so was not placed in $TARGET_FILE"
    exit 1
fi

echo "=== Native Linux module compiled successfully ==="