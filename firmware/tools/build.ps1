param(
    [ValidateSet('build','merge','all')][string]$Action='all',
    [string]$IdfPath=(Join-Path $env:USERPROFILE '.cache\nabo-fortune\esp-idf-v5.5.3'),
    [string]$VendorDir=(Join-Path $env:USERPROFILE '.cache\nabo-fortune\vendor-components'),
    [string]$ToolsPath=(Join-Path $env:USERPROFILE '.espressif')
)
$ErrorActionPreference='Stop'
$env:IDF_PATH=$IdfPath
$env:IDF_TOOLS_PATH=$ToolsPath
$env:IDF_PYTHON_ENV_PATH=Join-Path $ToolsPath 'python_env\idf5.5_py3.11_env'
$env:ESP_ROM_ELF_DIR=Join-Path $ToolsPath 'tools\esp-rom-elfs\20241011'
$env:IDF_COMPONENT_MANAGER='0'
$env:PYTHONUTF8='1'
$python=Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts\python.exe'
$prepend=@(
    (Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts'),
    (Join-Path $ToolsPath 'tools\cmake\3.30.2\bin'),
    (Join-Path $ToolsPath 'tools\ninja\1.12.1'),
    (Join-Path $ToolsPath 'tools\riscv32-esp-elf\esp-14.2.0_20251107\riscv32-esp-elf\bin')
)
$env:PATH=($prepend -join ';')+';'+$env:PATH
if (-not (Test-Path -LiteralPath $python)) {throw 'Missing ESP-IDF 5.5 Python environment'}
if (-not (Test-Path -LiteralPath (Join-Path $VendorDir 'button\CMakeLists.txt'))) {throw 'Missing vendor component directory'}
$fw=Split-Path -Parent $PSScriptRoot
Push-Location $fw
try {
    if ($Action -ne 'merge') {
        & $python (Join-Path $IdfPath 'tools\idf.py') '-DIDF_TARGET=esp32c3' "-DEXTRA_COMPONENT_DIRS=$($VendorDir.Replace('\','/'))" build
        if ($LASTEXITCODE -ne 0) {throw "Build failed: $LASTEXITCODE"}
    }
    if ($Action -ne 'build') {
        & $python (Join-Path $IdfPath 'tools\idf.py') merge-bin -o nabo-soundlab-merged.bin
        if ($LASTEXITCODE -ne 0) {throw "Merge failed: $LASTEXITCODE"}
    }
} finally { Pop-Location }
