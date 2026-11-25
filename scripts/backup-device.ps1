param(
    [string]$SizeHex = "0x200000",
    [string]$OutputPath = "",
    [switch]$SkipEnterUboot,
    [string]$Device = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$VenvPy = Join-Path $RepoRoot ".venv\Scripts\python.exe"
$EnterUbootPy = Join-Path $RepoRoot "scripts\enter_uboot.py"
$ToolDir = Join-Path $RepoRoot "tools\jl-uboot-tool"

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$id
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-IsAdmin)) {
    Write-Warning "Raw-SCSI-Zugriff über \\.\HardDiskVolume* benötigt unter Windows meist Administratorrechte."
    Write-Warning "Falls jluboottool.py mit PermissionError abbricht, PowerShell 'Als Administrator' starten."
}

if (-not (Test-Path $VenvPy)) {
    throw "Python-Venv fehlt unter $VenvPy."
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $ts = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputPath = Join-Path $RepoRoot "backups\t3-oem-dump-$ts.bin"
} elseif (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = Join-Path $RepoRoot $OutputPath
}

$outDir = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$expectedSize = [Convert]::ToInt64($SizeHex, 16)

function Find-JieLiUboot {
    return Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
        $_.InstanceId -match 'VID_4C4A' -or $_.FriendlyName -match 'UBOOT|BR35'
    }
}

$ubootDevs = Find-JieLiUboot
if (-not $ubootDevs -and -not $SkipEnterUboot) {
    Write-Host "[backup] Kein BR35UBOOT-Gerät aktiv — versuche Reboot via enter_uboot.py..." -ForegroundColor Cyan
    & $VenvPy $EnterUbootPy --reboot
    if ($LASTEXITCODE -ne 0) {
        throw "UBOOT-Trigger fehlgeschlagen (Exit $LASTEXITCODE)."
    }

    Write-Host "[backup] Warte bis zu 15s auf VID_4C4A (BR35UBOOT1.00)..." -ForegroundColor Cyan
    for ($i = 0; $i -lt 15; $i++) {
        Start-Sleep -Seconds 1
        $ubootDevs = Find-JieLiUboot
        if ($ubootDevs) { break }
    }
}

if (-not $ubootDevs) {
    Write-Warning "Kein VID_4C4A-Gerät erkannt (ggf. noch nicht neu enumeriert oder anderer Port)."
} else {
    Write-Host "[backup] Gefundenes UBOOT-Gerät:" -ForegroundColor Green
    $ubootDevs | Format-Table Status,Class,FriendlyName,InstanceId -AutoSize | Out-String | Write-Host
}

$toolArgs = @("jluboottool.py", "--chip", "br35")
if (-not [string]::IsNullOrWhiteSpace($Device)) {
    $toolArgs += @("--device", $Device)
}
$toolArgs += @("chipinfo", "read 0x0 $SizeHex `"$OutputPath`"", "reset")

Write-Host "[backup] Starte Flash-Dump ($SizeHex Bytes -> $OutputPath)..." -ForegroundColor Cyan
Push-Location $ToolDir
try {
    & $VenvPy @toolArgs
    if ($LASTEXITCODE -ne 0) {
        throw "jluboottool.py fehlgeschlagen (Exit $LASTEXITCODE)."
    }
}
finally {
    Pop-Location
}

if (-not (Test-Path $OutputPath)) {
    throw "Dump-Datei wurde nicht erzeugt: $OutputPath"
}

$actualSize = (Get-Item $OutputPath).Length
if ($actualSize -ne $expectedSize) {
    throw "Unerwartete Dump-Größe: $actualSize Bytes (erwartet: $expectedSize Bytes)!"
}

$sha = (Get-FileHash $OutputPath -Algorithm SHA256).Hash.ToLowerInvariant()
$shaFile = "$OutputPath.sha256"
"$sha  $(Split-Path -Leaf $OutputPath)" | Set-Content -Encoding ASCII $shaFile

# Plausibilitätsprüfung des Dumps (Header 'AC707N' @ 0x10 & nicht-leerer VM/BTIF-Bereich >= 0x138000)
& $VenvPy -c @"
from pathlib import Path
data = Path(r'$OutputPath').read_bytes()
hdr = data[0x10:0x16]
print(f'[backup] Header @ 0x10: {hdr!r}')
if b'AC707N' not in data[0x00:0x40]:
    raise SystemExit('WARNUNG: AC707N-Header bei 0x00..0x40 nicht gefunden!')
tail = data[0x138000:]
non_ff = sum(1 for b in tail if b != 0xFF)
print(f'[backup] Bereich >= 0x138000 (VM/BTIF/key_mac): {non_ff} von {len(tail)} Bytes != 0xFF')
if non_ff == 0:
    print('[backup] HINWEIS: Tail >= 0x138000 ist komplett 0xFF (oder ungenutzt).')
"@
if ($LASTEXITCODE -ne 0) {
    throw "Dump-Verifikation fehlgeschlagen!"
}

Write-Host "[backup] ERFOLG! Backup gespeichert unter: $OutputPath ($actualSize Bytes)" -ForegroundColor Green
Write-Host "[backup] SHA-256: $sha (gespeichert in $shaFile)" -ForegroundColor Green
