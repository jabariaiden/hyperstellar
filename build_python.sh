#!/bin/bash
set -e
echo "=== Building hyperstellar Python package (Linux) ==="

ORIGINAL_DIR=$(pwd)
BUILD_DIR="_build_linux"
GLFW_VERSION="3.4"

echo "1. Environment..."
ENV_SCRIPT="$HOME/hyperstellar_env/bin/activate"
if [ -f "$ENV_SCRIPT" ]; then source "$ENV_SCRIPT"; else echo "Running without venv"; fi

echo "2. Cleaning..."
rm -rf "$BUILD_DIR"

echo "3. Copying shaders..."
if [ -d "shaders" ]; then
    rm -rf "python_module/shaders"
    mkdir -p "python_module/shaders"
    cp -r shaders/. python_module/shaders/
else
    echo "ERROR: shaders/ not found"; exit 1
fi

echo "4. Fixing glad.c..."
if [ -f "include/glad/glad.c" ]; then cp "include/glad/glad.c" "src/"; fi

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
    -DGLFW_BUILD_DOCS=OFF \
    -DGLFW_BUILD_WAYLAND=OFF \
    -DGLFW_BUILD_OSMESA=OFF \
    -DGLFW_BUILD_X11=ON
make -j$(nproc)
cd ..
GLFW_ROOT="$(pwd)/glfw-build"
GLFW_INCLUDE_DIR="$(pwd)/glfw-${GLFW_VERSION}/include"
GLFW_LIBRARY="${GLFW_ROOT}/src/libglfw3.a"

echo "6. Building C++ module with static GLFW..."
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cmake ../python_module \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-DNO_TEXT_RENDERING -DPYTHON_MODULE=1" \
    -DGLFW_LIBRARY="${GLFW_LIBRARY}" \
    -DGLFW_INCLUDE_DIR="${GLFW_INCLUDE_DIR}"
cmake --build . -- -j$(nproc)

SO_FILE=$(find . -name "stellar*.so" | head -1)
if [ -z "$SO_FILE" ]; then echo "ERROR: No .so"; cd "$ORIGINAL_DIR"; exit 1; fi

TARGET_DIR="../hyperstellar/src/hyperstellar/_native/linux-x64"
mkdir -p "$TARGET_DIR/shaders"
cp "$SO_FILE" "$TARGET_DIR/stellar.so"
cp -r "../python_module/shaders/." "$TARGET_DIR/shaders/"
echo "   Native module built and copied."

cd "$ORIGINAL_DIR"

# ============================================================================
# 7. BUILD WHEEL
#
# THIS WHOLE STEP WAS MISSING from the version you last uploaded -- the
# script stopped after copying the .so, so unless your CI workflow builds
# the wheel itself in a separate step, nothing was being produced to
# upload to PyPI at all. Restored here.
#
# IMPORTANT FIX vs the old script:
# The old script force-relabeled every wheel as:
#     py3-none-<platform>
# claiming it is pure-Python and ABI-independent. But stellar.so is a
# compiled pybind11 extension linked against a *specific* CPython's C
# API (not the stable/limited ABI) -- it only works with the exact
# Python version + ABI it was built against. Forcing "py3-none" makes
# every matrix job (3.10, 3.11, 3.12, 3.13, 3.14) produce a wheel with
# the SAME filename, so:
#   - only one of them survives being uploaded to PyPI (the others get
#     skipped as duplicates, or overwrite/clobber each other)
#   - pip has no way to pick the right one per interpreter, so users on
#     a different Python version than whichever wheel "won" get an
#     ImportError like: undefined symbol: _PyThreadState_UncheckedGet
#
# Fix: let `build` auto-detect the correct interpreter/ABI tag
# (e.g. cp313-cp313) and only override the platform tag, so each
# matrix job uploads a distinctly-named, version-correct wheel.
# ============================================================================
echo "7. Building Python wheel..."
rm -rf dist build _wheel_build

python -m build --wheel --outdir _wheel_build .

RAW_WHEEL=$(find _wheel_build -name "*.whl" | head -1)
if [ -z "$RAW_WHEEL" ]; then
    echo "   ERROR: build produced no wheel"
    exit 1
fi

mkdir -p dist
RAW_WHEEL_DIR=$(dirname "$RAW_WHEEL")

# Only rewrite the platform tag (for manylinux-style naming). Leave the
# python-tag and abi-tag exactly as `build` detected them (e.g. cp313-cp313)
# since those reflect the real, version-specific ABI of stellar.so.
NEW_WHEEL_NAME=$(wheel tags --platform-tag linux_x86_64 --remove "$RAW_WHEEL" | tail -n 1)
mv "$RAW_WHEEL_DIR/$NEW_WHEEL_NAME" dist/

WHEEL=$(find dist -name "*.whl" | head -1)
echo "   Built: $(basename "$WHEEL")"

echo ""
echo "=== Done ==="
echo "Wheel: $WHEEL"