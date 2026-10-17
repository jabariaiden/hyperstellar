# build_python.ps1 - builds ONLY the native module (stellar.pyd) for the active Python.
# cibuildwheel runs this once per Python version (before-build), then builds the wheel itself.
$ErrorActionPreference = "Stop"
$ProgressPreference    = "SilentlyContinue"   # makes Invoke-WebRequest much faster

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $root
$buildDir = Join-Path $root "_build_windows"

function Check([string]$what) {
    if ($LASTEXITCODE -ne 0) { Write-Host "ERROR: $what failed (exit $LASTEXITCODE)"; exit 1 }
}

$py = (& python -c "import sys; print(sys.executable)").Trim(); Check "python"
Write-Host "=== Building hyperstellar native module (Windows) with $py ==="

Write-Host "1. Cleaning stale per-Python artifacts..."
# build/ and egg-info from a previous Python version must not leak into this wheel
Remove-Item -Recurse -Force $buildDir, "build" -ErrorAction SilentlyContinue
Get-ChildItem -Directory -Filter "*.egg-info" -Recurse -Depth 3 -ErrorAction SilentlyContinue |
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -Recurse -Force "hyperstellar\src\hyperstellar\_native\linux-x64" -ErrorAction SilentlyContinue
Remove-Item -Force "hyperstellar\src\hyperstellar\_native\windows-x64\stellar.pyd" -ErrorAction SilentlyContinue

Write-Host "2. GLFW..."
$glfwLib = "lib\glfw\glfw3.lib"
if (-not (Test-Path $glfwLib)) {
    $glfwZip = "glfw-3.4.bin.WIN64.zip"
    $ok = $false
    foreach ($try in 1..3) {
        try {
            Invoke-WebRequest -Uri "https://github.com/glfw/glfw/releases/download/3.4/$glfwZip" -OutFile $glfwZip
            $ok = $true; break
        } catch { Write-Host "   download attempt $try failed: $_"; Start-Sleep -Seconds 3 }
    }
    if (-not $ok) { Write-Host "ERROR: could not download GLFW"; exit 1 }
    Expand-Archive -Path $glfwZip -DestinationPath "glfw_temp" -Force
    New-Item -ItemType Directory -Force -Path "lib\glfw\include" | Out-Null
    Copy-Item -Path "glfw_temp\glfw-3.4.bin.WIN64\include\*" -Destination "lib\glfw\include\" -Recurse -Force
    $src = $null
    foreach ($d in "lib-vc2022", "lib-vc2019") {
        $cand = "glfw_temp\glfw-3.4.bin.WIN64\$d\glfw3.lib"
        if (Test-Path $cand) { $src = $cand; break }
    }
    if (-not $src) { Write-Host "ERROR: glfw3.lib not found in GLFW archive"; exit 1 }
    Copy-Item $src $glfwLib -Force
    Remove-Item -Recurse -Force "glfw_temp", $glfwZip -ErrorAction SilentlyContinue
}
if (-not (Test-Path $glfwLib)) { Write-Host "ERROR: $glfwLib missing"; exit 1 }

Write-Host "3. Shaders / glad..."
if (Test-Path "shaders") {
    Remove-Item -Recurse -Force "python_module\shaders" -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path "python_module\shaders" | Out-Null
    Copy-Item "shaders\*" "python_module\shaders\" -Recurse -Force
}
if (Test-Path "include\glad\glad.c") { Copy-Item "include\glad\glad.c" "src\" -Force }

Write-Host "4. pybind11 (pinned via pip; supports CPython 3.14)..."
& python -m pip install --quiet "pybind11>=3.0.1,<4"; Check "pip install pybind11"
$pbDir = (& python -m pybind11 --cmakedir).Trim(); Check "pybind11 --cmakedir"

Write-Host "5. Configuring + building with CMake..."
cmake -S python_module -B $buildDir `
    "-DCMAKE_CXX_FLAGS=/DNO_TEXT_RENDERING /DPYTHON_MODULE=1" `
    "-DPython3_EXECUTABLE=$py" `
    "-Dpybind11_DIR=$pbDir" `
    "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"
Check "CMake configure"

# CMake POST_BUILD copies stellar.pyd + shaders into hyperstellar\src\hyperstellar\_native\windows-x64
cmake --build $buildDir --config Release --parallel
Check "CMake build"

$target = "hyperstellar\src\hyperstellar\_native\windows-x64\stellar.pyd"
if (-not (Test-Path $target)) { Write-Host "ERROR: $target was not produced"; exit 1 }
Write-Host "=== Native Windows module built successfully ==="
exit 0