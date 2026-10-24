param(
    [ValidateSet('windows-msvc-debug', 'windows-msvc-release', 'headless-local')]
    [string]$Preset = 'windows-msvc-debug',
    [string]$GoogleTestSource = '',
    [string]$JsonSource = '',
    [string]$JsonSchemaSource = '',
    [switch]$CoreOnly
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
    if ($JsonSource) { $configureArgs += "-DSIM_JSON_SOURCE_DIR=$((Resolve-Path -LiteralPath $JsonSource).Path)" }
    if ($JsonSchemaSource) { $configureArgs += "-DSIM_JSON_SCHEMA_SOURCE_DIR=$((Resolve-Path -LiteralPath $JsonSchemaSource).Path)" }
    if ($CoreOnly) { $configureArgs += '-DSIM_BUILD_PROJECT=OFF' }
    & cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'C++ build failed.' }
    & ctest --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
} finally {
    Pop-Location
}
