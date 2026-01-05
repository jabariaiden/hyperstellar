# publish_to_pypi.ps1
# Run from: C:\Users\user\hyperstellar-public
# PURPOSE: Safely upload a pre-built package to PyPI.

Write-Host "=== Publishing Hyperstellar to PyPI ===" -ForegroundColor Cyan -BackgroundColor DarkRed
Write-Host "WARNING: This will publish a new version PUBLICLY to pypi.org" -ForegroundColor Red
Write-Host ""

# 1. Check that we have a dist/ folder with wheels
Write-Host "1. Checking for built packages..." -ForegroundColor Yellow
if (-not (Test-Path "dist")) {
    Write-Host "   ERROR: 'dist' folder not found." -ForegroundColor Red
    Write-Host "   You must run '.\build_python.ps1' first to build the package." -ForegroundColor Yellow
    exit 1
}

$wheels = Get-ChildItem dist\*.whl
if ($wheels.Count -eq 0) {
    Write-Host "   ERROR: No .whl files found in 'dist\'." -ForegroundColor Red
    Write-Host "   You must run '.\build_python.ps1' first to build the package." -ForegroundColor Yellow
    exit 1
}
Write-Host "   Found $($wheels.Count) wheel(s):" -ForegroundColor Green
$wheels | ForEach-Object { Write-Host "     - $($_.Name)" -ForegroundColor Gray }

# 2. Get PyPI Token (more secure than hardcoding)
Write-Host "`n2. PyPI Authentication..." -ForegroundColor Yellow
$PYPI_TOKEN = $env:PYPI_TOKEN
if (-not $PYPI_TOKEN) {
    Write-Host "   ERROR: PYPI_TOKEN environment variable is not set." -ForegroundColor Red
    Write-Host "   To set it for this session, run:" -ForegroundColor Yellow
    Write-Host "   PowerShell: `$env:PYPI_TOKEN = 'pypi-yourActualTokenHere'" -ForegroundColor Gray
    Write-Host "   Command Prompt: set PYPI_TOKEN=pypi-yourActualTokenHere" -ForegroundColor Gray
    Write-Host "   Then run this script again." -ForegroundColor Yellow
    exit 1
}
Write-Host "   Using token from environment." -ForegroundColor Green

# 3. FINAL CONFIRMATION
Write-Host "`n3. FINAL CONFIRMATION" -ForegroundColor Red -BackgroundColor Black
$packageName = (Get-Content pyproject.toml -Raw | Select-String -Pattern 'name = "(\w+)"').Matches.Groups[1].Value
$packageVersion = (Get-Content pyproject.toml -Raw | Select-String -Pattern 'version = "([\d.]+)"').Matches.Groups[1].Value

Write-Host "   You are about to publish:" -ForegroundColor Yellow
Write-Host "   PACKAGE : $packageName" -ForegroundColor White
Write-Host "   VERSION : $packageVersion" -ForegroundColor White
Write-Host "   TARGET  : The OFFICIAL PyPI (pypi.org)" -ForegroundColor White
Write-Host "`n   This action is PERMANENT. Versions cannot be deleted." -ForegroundColor Red

$confirm = Read-Host "   To proceed, type the word 'PUBLISH' exactly"
if ($confirm -ne 'PUBLISH') {
    Write-Host "   Publishing cancelled." -ForegroundColor Yellow
    exit 0
}

# 4. Perform the upload
Write-Host "`n4. Uploading to PyPI..." -ForegroundColor Yellow
$env:TWINE_USERNAME = "__token__"
$env:TWINE_PASSWORD = $PYPI_TOKEN

python -m twine upload dist/*

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n   ✅ SUCCESS! Published $packageName v$packageVersion to PyPI!" -ForegroundColor Green
    Write-Host "   View at: https://pypi.org/project/$packageName/" -ForegroundColor Gray
} else {
    Write-Host "`n   ❌ Upload failed." -ForegroundColor Red
}