param(
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$FetchScript = Join-Path $PSScriptRoot "fetch_sdk.py"

Push-Location $RepoRoot
try {
    $argsList = @($FetchScript)
    if ($Force) { $argsList += "--force" }
    & python @argsList
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
