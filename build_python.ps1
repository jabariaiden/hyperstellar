# build_python.ps1
# Run from: C:\Users\user\hyperstellar-public

Write-Host "=== Building hyperstellar Python package ===" -ForegroundColor Cyan

# 1. ACTIVATE ENVIRONMENT (optional, skipped in CI)
Write-Host "1. Activating Python environment..." -ForegroundColor Yellow
$envScript = "C:\Users\user\jabariaiden-ProjStellar-main\hyperstellar_env\Scripts\Activate.ps1"
if (Test-Path $envScript) {
    . $envScript
    Write-Host "   ✓ Activated hyperstellar_env" -ForegroundColor Green
} else {
    Write-Host "   ✓ Running without venv (CI mode)" -ForegroundColor Gray
}

$originalDir = Get-Location
$BUILD_DIR = "_build"
Write-Host "   Working from: $originalDir" -ForegroundColor Gray

# 2. CLEAN
Write-Host "2. Cleaning..." -ForegroundColor Yellow
Remove-Item dist, build, $BUILD_DIR -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "   ✓ Cleaned" -ForegroundColor Green

# 3. COPY SHADERS
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

# 4. FIX GLAD.C
Write-Host "4. Fixing source paths..." -ForegroundColor Yellow
if (Test-Path "include\glad\glad.c") {
    Copy-Item "include\glad\glad.c" "src\" -Force
    Write-Host "   ✓ Copied glad.c to src\" -ForegroundColor Green
} elseif (Test-Path "src\glad.c") {
    Write-Host "   ✓ glad.c already in src\" -ForegroundColor Green
} else {
    Write-Host "   WARNING: glad.c not found" -ForegroundColor Yellow
}

# 5. BUILD C++ MODULE
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

# 6. BUILD WHEEL
Write-Host "6. Building Python wheel..." -ForegroundColor Yellow
Remove-Item dist, build, _wheel_build -Recurse -Force -ErrorAction SilentlyContinue

python -m build --wheel --outdir _wheel_build .

$rawWheel = Get-ChildItem _wheel_build\*.whl | Select-Object -First 1
if (-not $rawWheel) {
    Write-Host "   ERROR: build produced no wheel" -ForegroundColor Red
    exit 1
}

mkdir dist -Force | Out-Null
wheel tags --python-tag py3 --abi-tag none --platform-tag win_amd64 -o dist --remove $rawWheel.FullName

$wheel = Get-ChildItem dist\*.whl | Select-Object -First 1
Write-Host "   ✓ Built: $($wheel.Name)" -ForegroundColor Green

Write-Host "`n=== Done ===" -ForegroundColor Cyan