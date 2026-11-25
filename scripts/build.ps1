param(
    [ValidateSet("bringup", "doom", "henner", "meter")]
    [string]$Project = "bringup",
    [switch]$Clean,
    [int]$Jobs = [Environment]::ProcessorCount,
    [string]$MakeArgs = "",
    [string]$SlotOut = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $SlotOut) {
    $SlotOut = switch ($Project) {
        "doom"   { "firmware-builds\t3-doom-slot.bin" }
        "henner" { "firmware-builds\t3-hennergotchi-slot.bin" }
        "meter"  { "firmware-builds\t3-meter-fw-slot.bin" }
        default  { "firmware-builds\t3-bsp-bringup-slot.bin" }
    }
}

Push-Location $RepoRoot
try {
    docker info *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "Docker Daemon läuft nicht. Bitte Docker Desktop starten und erneut versuchen."
    }

    if (-not (Test-Path "sdk\ac707n\SDK\Makefile")) {
        throw "SDK fehlt unter sdk\ac707n\SDK\Makefile. Bitte 'python scripts/fetch_sdk.py' ausführen."
    }

    $Py = Join-Path $RepoRoot ".venv\Scripts\python.exe"
    if (-not (Test-Path $Py)) {
        throw ".venv\Scripts\python.exe fehlt."
    }

    Write-Host "[T3-Build] Wende shared/t3-bsp Overlay ($Project) auf AC707N SDK an..." -ForegroundColor Cyan
    & $Py (Join-Path $RepoRoot "scripts\apply_t3_bsp.py") --project $Project
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $cleanCmd = if ($Clean) { "make -C /workspace/sdk/ac707n/SDK clean && " } else { "" }
    $cmd = "mkdir -p /workspace/sdk/ac707n/SDK/tools/linux && cp -an /opt/jieli/postbuild/. /workspace/sdk/ac707n/SDK/tools/linux/ && ${cleanCmd}make -C /workspace/sdk/ac707n/SDK -j${Jobs} ${MakeArgs}"

    Write-Host "[T3-Build] Starte Build im Docker-Container (Jobs=$Jobs)..." -ForegroundColor Cyan
    docker compose run --rm builder bash -lc $cmd
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    $AppBin = Join-Path $RepoRoot "sdk\ac707n\SDK\cpu\br35\tools\app.bin"
    if (-not (Test-Path $AppBin)) {
        throw "Build beendet, aber $AppBin wurde nicht erzeugt!"
    }

    Write-Host "[T3-Build] Pruefe & packe 0x5000-Flash-Slot ($SlotOut)..." -ForegroundColor Cyan
    & $Py (Join-Path $RepoRoot "scripts\pack_t3_slot.py") --self-test --app $AppBin --out (Join-Path $RepoRoot $SlotOut)
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Pop-Location
}
