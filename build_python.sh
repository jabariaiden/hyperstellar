#!/bin/bash
set -e
echo "=== Building hyperstellar native module (Linux) ==="

ORIGINAL_DIR=$(pwd)
BUILD_DIR="_build_linux"
GLFW_VERSION="3.4"

# Clean build directory and opposite platform native binaries to prevent wheel contamination
rm -rf "$BUILD_DIR"
rm -rf "hyperstellar/src/hyperstellar/_native/windows-x64"

echo "1. Preparing source files..."
if [ -d "shaders" ]; then
    rm -rf "python_module/shaders"
    mkdir -p "python_module/shaders"
    cp -r shaders/. python_module/shaders/
else
    echo "ERROR: shaders/ directory not found"; exit 1
fi

if [ -f "include/glad/glad.c" ]; then cp "include/glad/glad.c" "src/"; fi

echo "2. Building GLFW static library..."
if [ ! -f "glfw-build/src/libglfw3.a" ]; then
    wget -q "https://github.com/glfw/glfw/releases/download/${GLFW_VERSION}/glfw-${GLFW_VERSION}.tar.gz"
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

CURRENT_PYTHON=$(which python)
echo "3. Building C++ extension for active Python: ${CURRENT_PYTHON}..."

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake ../python_module \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-DNO_TEXT_RENDERING -DPYTHON_MODULE=1" \
    -DPython3_EXECUTABLE="${CURRENT_PYTHON}" \
    -DGLFW_LIBRARY="${GLFW_LIBRARY}" \
    -DGLFW_INCLUDE_DIR="${GLFW_INCLUDE_DIR}"

cmake --build . -- -j$(nproc)

SO_FILE=$(find . -name "stellar*.so" | head -1)
if [ -z "$SO_FILE" ]; then echo "ERROR: No .so file produced"; cd "$ORIGINAL_DIR"; exit 1; fi

TARGET_DIR="../hyperstellar/src/hyperstellar/_native/linux-x64"
mkdir -p "$TARGET_DIR/shaders"
cp "$SO_FILE" "$TARGET_DIR/stellar.so"
cp -r "../python_module/shaders/." "$TARGET_DIR/shaders/"

cd "$ORIGINAL_DIR"
echo "=== Native Linux module compiled successfully ==="