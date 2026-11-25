$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

Push-Location $RepoRoot
try {
    docker info *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "Docker Daemon läuft nicht. Bitte Docker Desktop starten und erneut versuchen."
    }

    Write-Host "[T3-Shell] Öffne Linux-Build-Shell (ulimit -n 65536, pi32v2-clang im PATH)..." -ForegroundColor Cyan
    docker compose run --rm builder bash
}
finally {
    Pop-Location
}
