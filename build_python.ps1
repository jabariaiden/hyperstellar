# build_python.ps1 - Builds the native C++ module for Windows.
# It copies shaders, prepares sources, runs CMake, and places the final
# stellar.pyd and shaders into the Python package source tree.
# This script does NOT build the wheel; that is handled by cibuildwheel.

Write-Host "=== Building hyperstellar native module (Windows) ===" -ForegroundColor Cyan

# ------------------------------------------------------------------------
# 1. Activate environment (optional, skipped in CI)
# ------------------------------------------------------------------------
Write-Host "1. Activating Python environment..." -ForegroundColor Yellow
$envScript = "C:\Users\user\jabariaiden-ProjStellar-main\hyperstellar_env\Scripts\Activate.ps1"
if (Test-Path $envScript) {
    . $envScript
    Write-Host "   ✓ Activated hyperstellar_env" -ForegroundColor Green
} else {
    Write-Host "   ✓ Running without venv (CI mode)" -ForegroundColor Gray
}

$originalDir = Get-Location
$BUILD_DIR = "_build_windows"
Write-Host "   Working from: $originalDir" -ForegroundColor Gray

# ------------------------------------------------------------------------
# 2. Clean
# ------------------------------------------------------------------------
Write-Host "2. Cleaning..." -ForegroundColor Yellow
Remove-Item -Recurse -Force $BUILD_DIR -ErrorAction SilentlyContinue
Write-Host "   ✓ Cleaned" -ForegroundColor Green

# ------------------------------------------------------------------------
# 3. Copy shaders into python_module/shaders (CMake will copy them later)
# ------------------------------------------------------------------------
Write-Host "3. Copying shaders..." -ForegroundColor Yellow
$rootShaders = "shaders"
$cmakeShaders = "python_module\shaders"

if (Test-Path $rootShaders) {
    Remove-Item $cmakeShaders -Recurse -Force -ErrorAction SilentlyContinue
    mkdir $cmakeShaders -Force
    Copy-Item "$rootShaders\*" $cmakeShaders -Recurse -Force
    $rootCount = (Get-ChildItem $rootShaders -File -Recurse).Count
    $cmakeCount = (Get-ChildItem $cmakeShaders -File -Recurse).Count
    if ($rootCount -eq $cmakeCount) {
        Write-Host "   ✓ Copied $rootCount shader files" -ForegroundColor Green
    } else {
        Write-Host "   WARNING: File count mismatch (root: $rootCount, cmake: $cmakeCount)" -ForegroundColor Yellow
    }
} else {
    Write-Host "   ERROR: shaders/ not found" -ForegroundColor Red
    exit 1
}

# ------------------------------------------------------------------------
# 4. Ensure glad.c is in src/
# ------------------------------------------------------------------------
Write-Host "4. Fixing source paths..." -ForegroundColor Yellow
if (Test-Path "include\glad\glad.c") {
    Copy-Item "include\glad\glad.c" "src\" -Force
    Write-Host "   ✓ Copied glad.c to src\" -ForegroundColor Green
} elseif (Test-Path "src\glad.c") {
    Write-Host "   ✓ glad.c already in src\" -ForegroundColor Green
} else {
    Write-Host "   WARNING: glad.c not found" -ForegroundColor Yellow
}

# ------------------------------------------------------------------------
# 5. Build the native module with CMake
# ------------------------------------------------------------------------
Write-Host "5. Building C++ module..." -ForegroundColor Yellow
mkdir $BUILD_DIR -Force
cd $BUILD_DIR

try {
    Write-Host "   Configuring..." -ForegroundColor Gray
    cmake ../python_module -DCMAKE_CXX_FLAGS="/DNO_TEXT_RENDERING /DPYTHON_MODULE=1"

    if ($LASTEXITCODE -eq 0) {
        Write-Host "   Building..." -ForegroundColor Gray
        cmake --build . --config Release

        $pydFile = Get-ChildItem -Recurse -Filter "stellar*.pyd" | Select-Object -First 1
        if ($pydFile) {
            $targetDir = "..\hyperstellar\src\hyperstellar\_native\windows-x64"
            mkdir $targetDir -Force
            Copy-Item $pydFile.FullName "$targetDir\stellar.pyd" -Force
            $packageShaders = "$targetDir\shaders"
            mkdir $packageShaders -Force
            Copy-Item "..\python_module\shaders\*" $packageShaders -Recurse -Force
            Write-Host "   ✓ Built stellar.pyd and copied shaders" -ForegroundColor Green
        } else {
            Write-Host "   ERROR: stellar.pyd not found after build" -ForegroundColor Red
            cd $originalDir
            exit 1
        }
    } else {
        Write-Host "   ERROR: CMake configure failed" -ForegroundColor Red
        cd $originalDir
        exit 1
    }
} catch {
    Write-Host "   Build failed: $_" -ForegroundColor Red
    cd $originalDir
    exit 1
}

cd $originalDir
Write-Host "=== Native module built successfully ===" -ForegroundColor Cyan