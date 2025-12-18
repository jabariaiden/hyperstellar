# build_python.ps1
# Run from: C:\Users\user\hyperstellar-public

Write-Host "=== Building hyperstellar Python package ===" -ForegroundColor Cyan

# 1. ACTIVATE ENVIRONMENT
Write-Host "1. Activating Python environment..." -ForegroundColor Yellow
$envScript = "C:\Users\user\jabariaiden-ProjStellar-main\hyperstellar_env\Scripts\Activate.ps1"
if (Test-Path $envScript) {
    . $envScript
    Write-Host "   ✓ Activated hyperstellar_env" -ForegroundColor Green
} else {
    Write-Host "   ERROR: hyperstellar_env not found!" -ForegroundColor Red
    exit 1
}

# Store ORIGINAL directory (should be hyperstellar-public/)
$originalDir = Get-Location
Write-Host "   Working from: $originalDir" -ForegroundColor Gray

# Configuration
$BUILD_DIR = "_build"
$TESTPYPI_TOKEN = "pypi-AgENdGVzdC5weXBpLm9yZwIkMTQyNDhjMDUtMzM1Ni00MzE2LTk2MDctYjBkMWVjZTYyZDNmAAIqWzMsIjcyY2RhMDc5LTQ0YWUtNDA3ZS04OTlkLTA4MGI3MTlkOGIwYSJdAAAGIK9IIVfH4NxJ4vPpiJIIUzF-aFeqxe6bQAMJPXnDuATQ"

# 2. Clean - ONLY clean in current directory (root)
Write-Host "2. Cleaning..." -ForegroundColor Yellow
Remove-Item dist, build, $BUILD_DIR -Recurse -Force -ErrorAction SilentlyContinue

# 3. Copy shaders from root to python_module BEFORE building
Write-Host "3. Copying shaders..." -ForegroundColor Yellow
$rootShaders = "shaders"
$cmakeShaders = "python_module\shaders"

if (Test-Path $rootShaders) {
    # Clean CMake shader directory first
    Remove-Item $cmakeShaders -Recurse -Force -ErrorAction SilentlyContinue
    
    # Copy all shader files
    mkdir $cmakeShaders -Force
    Copy-Item "$rootShaders\*" $cmakeShaders -Recurse -Force
    
    # Verify copy
    $rootCount = (Get-ChildItem $rootShaders -File -Recurse).Count
    $cmakeCount = (Get-ChildItem $cmakeShaders -File -Recurse).Count
    
    if ($rootCount -eq $cmakeCount) {
        Write-Host "   ✓ Copied $rootCount shader files to python_module/shaders" -ForegroundColor Green
    } else {
        Write-Host "   WARNING: File count mismatch (root: $rootCount, cmake: $cmakeCount)" -ForegroundColor Yellow
    }
} else {
    Write-Host "   ERROR: Shaders not found in $rootShaders" -ForegroundColor Red
    exit 1
}

# 4. Fix glad.c path for CMake
Write-Host "4. Fixing source paths..." -ForegroundColor Yellow
if (Test-Path "include\glad\glad.c") {
    # Copy glad.c to where CMake expects it
    Copy-Item "include\glad\glad.c" "src\" -Force
    Write-Host "   ✓ Copied glad.c to src\" -ForegroundColor Green
} elseif (Test-Path "src\glad.c") {
    Write-Host "   ✓ glad.c already in src\" -ForegroundColor Green
} else {
    Write-Host "   WARNING: glad.c not found" -ForegroundColor Yellow
}

# 5. Temporary CMake patch to disable standalone
Write-Host "5. Creating temporary CMake patch..." -ForegroundColor Yellow
$cmakeOriginal = Get-Content "python_module\CMakeLists.txt" -Raw
# Disable the problematic add_executable line
$cmakePatched = $cmakeOriginal -replace 'add_executable\(stellar_main \$\{CORE_SOURCES\} \$\{IMGUI_SOURCES\} src/main\.cpp\)', '# add_executable(stellar_main ${CORE_SOURCES} ${IMGUI_SOURCES} src/main.cpp) # COMMENTED OUT'
$cmakePatched | Set-Content "python_module\CMakeLists.txt.temp" -Encoding UTF8

# 6. Build C++ module with patched CMake
Write-Host "6. Building C++ module..." -ForegroundColor Yellow
mkdir $BUILD_DIR -Force
cd $BUILD_DIR

try {
    Write-Host "   Configuring..." -ForegroundColor Gray
    # Use the patched CMakeLists.txt
    Copy-Item "..\python_module\CMakeLists.txt.temp" "..\python_module\CMakeLists.txt" -Force
    cmake ../python_module -DCMAKE_CXX_FLAGS="/DNO_TEXT_RENDERING /DPYTHON_MODULE=1"
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "   Building..." -ForegroundColor Gray
        cmake --build . --config Release
        
        # Find .pyd
        $pydFile = Get-ChildItem -Recurse -Filter "stellar.pyd" | Select-Object -First 1
        if ($pydFile) {
            $targetDir = "..\hyperstellar\src\hyperstellar\_native\windows-x64"
            mkdir $targetDir -Force
            
            # Copy .pyd
            Copy-Item $pydFile.FullName "$targetDir\stellar.pyd" -Force
            Write-Host "   ✓ Built new .pyd" -ForegroundColor Green
            
            # COPY SHADERS TO PACKAGE (Critical!)
            $packageShaders = "$targetDir\shaders"
            mkdir $packageShaders -Force
            Copy-Item "..\python_module\shaders\*" $packageShaders -Recurse -Force
            Write-Host "   ✓ Copied shaders to package" -ForegroundColor Green
        }
    }
} catch {
    Write-Host "   Build failed: $_" -ForegroundColor Red
}
finally {
    # Restore original CMakeLists.txt
    Copy-Item "..\python_module\CMakeLists.txt.temp" "..\python_module\CMakeLists.txt" -Force
    Remove-Item "..\python_module\CMakeLists.txt.temp" -Force -ErrorAction SilentlyContinue
}

# RETURN TO ORIGINAL DIRECTORY (root)
cd $originalDir

# 7. Update version - pyproject.toml is in ROOT
Write-Host "7. Updating version..." -ForegroundColor Yellow
$pyprojectPath = "pyproject.toml"
if (Test-Path $pyprojectPath) {
    $tomlContent = Get-Content $pyprojectPath -Raw
    if ($tomlContent -match 'version = "([^"]+)"') {
        $currentVersion = $matches[1]
        $versionParts = $currentVersion -split '\.'
        $newVersion = "$($versionParts[0]).$($versionParts[1]).$([int]$versionParts[2] + 1)"
        
        $tomlContent = $tomlContent -replace 'version = "[^"]+"', "version = `"$newVersion`"" 
        $tomlContent | Set-Content $pyprojectPath
        
        Write-Host "   Version: $currentVersion -> $newVersion" -ForegroundColor Green
    } else {
        $newVersion = "0.1.0"
        Write-Host "   Using default version: $newVersion" -ForegroundColor Yellow
    }
} else {
    Write-Host "   ERROR: pyproject.toml not found at $pyprojectPath" -ForegroundColor Red
    Write-Host "   Current directory: $(Get-Location)" -ForegroundColor Gray
    exit 1
}

# 8. Build Python wheel - FROM ROOT DIRECTORY (where pyproject.toml is)
Write-Host "8. Building Python wheel..." -ForegroundColor Yellow
python -m build
if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Python build failed" -ForegroundColor Red
    exit 1
}

$wheel = Get-ChildItem dist\*.whl | Select-Object -First 1
Write-Host "   Built: $($wheel.Name)" -ForegroundColor Gray

# 9. SIMPLE TEST - dist is now in ROOT
Write-Host "9. Running simple test..." -ForegroundColor Yellow
python -c "
import sys
print('Python:', sys.version)
try:
    import hyperstellar
    print('✓ Import successful')
    print('  Version:', hyperstellar.__version__)
    
    # Basic functionality test
    sim = hyperstellar.Simulation(headless=True)
    print('✓ Simulation created')
    
    obj_id = sim.add_object(x=0, y=0, mass=1.0, skin=hyperstellar.SkinType.CIRCLE)
    print(f'✓ Object added (ID: {obj_id})')
    
    sim.set_equation(obj_id, '0, 0')
    print('✓ Equation set')
    
    sim.update(0.01)
    print('✓ Physics update worked')
    
    state = sim.get_object(obj_id)
    print(f'✓ Object state: ({state.x:.2f}, {state.y:.2f})')
    
    sim.cleanup()
    print('✓ Cleanup successful')
    
    print('\\n✓ ALL TESTS PASSED!')
    
except Exception as e:
    print(f'✗ Test failed: {e}')
    import traceback
    traceback.print_exc()
    sys.exit(1)
"

# 10. Upload - dist is in ROOT
Write-Host "`n10. Upload to TestPyPI?" -ForegroundColor Cyan
$choice = Read-Host "(y/n)"
if ($choice -eq 'y') {
    Write-Host "   Uploading..." -ForegroundColor Yellow
    
    $env:TWINE_USERNAME = "__token__"
    $env:TWINE_PASSWORD = $TESTPYPI_TOKEN
    
    python -m twine upload --repository testpypi dist/*
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "   ✓ Uploaded to TestPyPI!" -ForegroundColor Green
        Write-Host "   View at: https://test.pypi.org/project/hyperstellar/$newVersion/" -ForegroundColor Gray
    }
}

Write-Host "`n=== Done ===" -ForegroundColor Cyan
Write-Host "Clean up: Remove-Item _build, dist, build -Recurse -Force" -ForegroundColor Gray