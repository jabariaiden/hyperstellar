# build_python.ps1 - Builds native C++ module for Windows
Write-Host "=== Building hyperstellar native module (Windows) ===" -ForegroundColor Cyan

$originalDir = Get-Location
$BUILD_DIR = "_build_windows"

# Clean build directory and opposite platform native binaries
Remove-Item -Recurse -Force $BUILD_DIR -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force "hyperstellar\src\hyperstellar\_native\linux-x64" -ErrorAction SilentlyContinue

Write-Host "1. Preparing source files..." -ForegroundColor Yellow
$rootShaders = "shaders"
$cmakeShaders = "python_module\shaders"

if (Test-Path $rootShaders) {
    Remove-Item $cmakeShaders -Recurse -Force -ErrorAction SilentlyContinue
    mkdir $cmakeShaders -Force
    Copy-Item "$rootShaders\*" $cmakeShaders -Recurse -Force
} else {
    Write-Host "   ERROR: shaders/ directory not found" -ForegroundColor Red
    exit 1
}

if (Test-Path "include\glad\glad.c") {
    Copy-Item "include\glad\glad.c" "src\" -Force
}

Write-Host "2. Building C++ module with CMake..." -ForegroundColor Yellow
mkdir $BUILD_DIR -Force
cd $BUILD_DIR

# Bind CMake directly to cibuildwheel's active Python interpreter
$pythonExe = (Get-Command python).Source

try {
    Write-Host "   Configuring for active Python: $pythonExe" -ForegroundColor Gray
    cmake ../python_module `
        -DCMAKE_CXX_FLAGS="/DNO_TEXT_RENDERING /DPYTHON_MODULE=1" `
        -DPython3_EXECUTABLE="$pythonExe"

    if ($LASTEXITCODE -eq 0) {
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
Write-Host "=== Native Windows module compiled successfully ===" -ForegroundColor Cyan