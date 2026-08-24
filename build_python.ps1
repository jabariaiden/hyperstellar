# build_python.ps1 - Builds native C++ module for Windows
$ErrorActionPreference = "Continue"
Write-Host "=== Building hyperstellar native module (Windows) ===" -ForegroundColor Cyan

$originalDir = Get-Location
$BUILD_DIR = "_build_windows"

# Clean build directories and stale linux binaries
Remove-Item -Recurse -Force $BUILD_DIR -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force "hyperstellar\src\hyperstellar\_native\linux-x64" -ErrorAction SilentlyContinue

Write-Host "1. Checking GLFW dependency..." -ForegroundColor Yellow
$glfwLib = "lib\glfw\glfw3.lib"
if (-not (Test-Path $glfwLib)) {
    Write-Host "   Downloading prebuilt GLFW 3.4 binaries..." -ForegroundColor Gray
    $glfwZip = "glfw-3.4.bin.WIN64.zip"
    Invoke-WebRequest -Uri "https://github.com/glfw/glfw/releases/download/3.4/glfw-3.4.bin.WIN64.zip" -OutFile $glfwZip
    Expand-Archive -Path $glfwZip -DestinationPath "glfw_temp" -Force
    
    New-Item -ItemType Directory -Force -Path "lib\glfw\include" | Out-Null
    Copy-Item -Path "glfw_temp\glfw-3.4.bin.WIN64\include\*" -Destination "lib\glfw\include\" -Recurse -Force
    
    if (Test-Path "glfw_temp\glfw-3.4.bin.WIN64\lib-vc2022\glfw3.lib") {
        Copy-Item -Path "glfw_temp\glfw-3.4.bin.WIN64\lib-vc2022\glfw3.lib" -Destination "lib\glfw\glfw3.lib" -Force
    } elseif (Test-Path "glfw_temp\glfw-3.4.bin.WIN64\lib-vc2019\glfw3.lib") {
        Copy-Item -Path "glfw_temp\glfw-3.4.bin.WIN64\lib-vc2019\glfw3.lib" -Destination "lib\glfw\glfw3.lib" -Force
    }
    
    Remove-Item -Recurse -Force "glfw_temp", $glfwZip -ErrorAction SilentlyContinue
    Write-Host "   ✓ GLFW configured" -ForegroundColor Green
}

Write-Host "2. Preparing source files..." -ForegroundColor Yellow
if (Test-Path "include\glad\glad.c") {
    Copy-Item "include\glad\glad.c" "src\" -Force
}

Write-Host "3. Building C++ module with CMake..." -ForegroundColor Yellow
mkdir $BUILD_DIR -Force | Out-Null
cd $BUILD_DIR

$pythonExe = $(python -c "import sys; print(sys.executable)")
Write-Host "   Configuring for active Python: $pythonExe" -ForegroundColor Gray

# Configure CMake
cmake ../python_module `
    -DCMAKE_CXX_FLAGS="/DNO_TEXT_RENDERING /DPYTHON_MODULE=1" `
    -DPython3_EXECUTABLE="$pythonExe"
if ($LASTEXITCODE -ne 0) {
    Write-Host "   ERROR: CMake configure failed" -ForegroundColor Red
    cd $originalDir
    exit 1
}

# Build CMake (CMake POST_BUILD automatically copies the .pyd and shaders)
cmake --build . --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Host "   ERROR: CMake build failed" -ForegroundColor Red
    cd $originalDir
    exit 1
}

cd $originalDir

Write-Host "4. Verifying CMake POST_BUILD copied files..." -ForegroundColor Yellow
$targetFile = "hyperstellar\src\hyperstellar\_native\windows-x64\stellar.pyd"
if (-not (Test-Path $targetFile)) {
    Write-Host "   ERROR: stellar.pyd was not placed in $targetFile" -ForegroundColor Red
    exit 1
}

Write-Host "=== Native Windows module built successfully ===" -ForegroundColor Cyan