param(
    [ValidateSet('windows-msvc-debug', 'windows-msvc-release', 'headless-local')]
    [string]$Preset = 'windows-msvc-debug',
    [string]$GoogleTestSource = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
Push-Location $repoRoot
try {
    $configureArgs = @('--preset', $Preset)
    if ($GoogleTestSource) {
        $resolvedSource = (Resolve-Path -LiteralPath $GoogleTestSource).Path
        $configureArgs += "-DSIM_GTEST_SOURCE_DIR=$resolvedSource"
    }
    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
    & ctest --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
} finally {
    Pop-Location
}
