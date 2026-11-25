param(
    [Alias("SlotBin")]
    [string]$SlotPath = "firmware-builds\t3-bsp-bringup-slot.bin",
    [switch]$RestoreOem,
    [switch]$SkipReboot,
    [switch]$NoReset,
    [string]$Device = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$id
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Find-JieLiUboot {
    return Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object {
        $_.InstanceId -match 'VID_4C4A' -or $_.FriendlyName -match 'UBOOT|BR35'
    }
}

Push-Location $RepoRoot
try {
    if (-not (Test-IsAdmin)) {
        Write-Warning "Raw-SCSI-Zugriff ueber \\.\HardDiskVolume* benoetigt unter Windows meist Administratorrechte."
        Write-Warning "Falls jluboottool.py mit PermissionError abbricht, PowerShell 'Als Administrator' starten."
    }

    $Py = Join-Path $RepoRoot ".venv\Scripts\python.exe"
    $ToolDir = Join-Path $RepoRoot "tools\jl-uboot-tool"
    $UbootTool = Join-Path $ToolDir "jluboottool.py"
    $EnterUboot = Join-Path $RepoRoot "scripts\enter_uboot.py"

    if (-not (Test-Path $Py)) { throw ".venv\Scripts\python.exe fehlt." }
    if (-not (Test-Path $UbootTool)) { throw "tools\jl-uboot-tool\jluboottool.py fehlt." }

    # Safety: refuse to flash unless at least one full 2 MB OEM backup exists
    $Backups = Get-ChildItem -Path (Join-Path $RepoRoot "backups") -Filter "t3-oem-dump-*.bin" -ErrorAction SilentlyContinue |
        Where-Object { $_.Length -eq 2097152 } |
        Sort-Object LastWriteTime -Descending

    if (-not $Backups -or $Backups.Count -eq 0) {
        throw "Kein gueltiges 2-MB-Backup unter backups\t3-oem-dump-*.bin gefunden! Zuerst .\scripts\backup-device.ps1 ausfuehren."
    }

    $TempDir = Join-Path $RepoRoot "firmware-builds\.flash-tmp"
    New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

    if ($RestoreOem) {
        $LatestBackup = $Backups[0].FullName
        $SlotToFlash = Join-Path $TempDir "oem-restore-slot.bin"
        Write-Host "[T3-Flash] Extrahiere OEM-Slot 0x5000..0x138000 aus $LatestBackup..." -ForegroundColor Cyan
        & $Py -c "import pathlib; b=pathlib.Path(r'$LatestBackup').read_bytes(); pathlib.Path(r'$SlotToFlash').write_bytes(b[0x5000:0x138000])"
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    else {
        $Resolved = Resolve-Path $SlotPath -ErrorAction Stop
        $SlotToFlash = $Resolved.Path
    }

    $SlotLen = (Get-Item $SlotToFlash).Length
    if ($SlotLen -le 0x100 -or $SlotLen -gt 0x133000) {
        throw "Ungueltige Slot-Groesse $SlotLen Bytes (erwartet 0x101..0x133000 fuer 0x5000..0x138000)."
    }
    $LenHex = "0x{0:X}" -f $SlotLen

    $ubootDevs = Find-JieLiUboot
    if (-not $ubootDevs -and -not $SkipReboot) {
        Write-Host "[T3-Flash] Kein BR35UBOOT-Geraet aktiv - versuche Reboot via enter_uboot.py..." -ForegroundColor Yellow
        & $Py $EnterUboot --reboot
        if ($LASTEXITCODE -ne 0) {
            throw "enter_uboot.py --reboot fehlgeschlagen (exit $LASTEXITCODE)."
        }

        Write-Host "[T3-Flash] Warte bis zu 15s auf VID_4C4A (BR35UBOOT1.00)..." -ForegroundColor Cyan
        for ($i = 0; $i -lt 15; $i++) {
            Start-Sleep -Seconds 1
            $ubootDevs = Find-JieLiUboot
            if ($ubootDevs) { break }
        }
    }

    if (-not $ubootDevs) {
        Write-Warning "Kein VID_4C4A-Geraet erkannt (ggf. noch nicht neu enumeriert oder anderer Port)."
    } else {
        Write-Host "[T3-Flash] Gefundenes UBOOT-Geraet:" -ForegroundColor Green
        $ubootDevs | Format-Table Status,Class,FriendlyName,InstanceId -AutoSize | Out-String | Write-Host
    }

    $VerifyFile = Join-Path $TempDir "verify-readback.bin"
    if (Test-Path $VerifyFile) { Remove-Item $VerifyFile -Force }

    $toolArgs = @("jluboottool.py", "--chip", "br35")
    if (-not [string]::IsNullOrWhiteSpace($Device)) {
        $toolArgs += @("--device", $Device)
    }
    $toolArgs += @(
        "chipinfo",
        "write 0x5000 $SlotToFlash",
        "read 0x5000 $LenHex $VerifyFile"
    )
    if (-not $NoReset) {
        $toolArgs += "reset"
    }

    Write-Host "[T3-Flash] Schreibe $SlotLen Bytes ($LenHex) nach 0x5000 und fuehre Readback-Verifikation durch..." -ForegroundColor Yellow
    Push-Location $ToolDir
    try {
        & $Py @toolArgs
        if ($LASTEXITCODE -ne 0) {
            throw "jluboottool.py fehlgeschlagen (exit $LASTEXITCODE)."
        }
    }
    finally {
        Pop-Location
    }

    if (-not (Test-Path $VerifyFile)) {
        throw "Readback-Datei wurde nicht erzeugt: $VerifyFile"
    }
    $VerifyLen = (Get-Item $VerifyFile).Length
    if ($VerifyLen -ne $SlotLen) {
        throw "VERIFIKATIONSFEHLER: Readback-Groesse $VerifyLen != $SlotLen Bytes!"
    }

    $HashSrc = (Get-FileHash $SlotToFlash -Algorithm SHA256).Hash.ToLowerInvariant()
    $HashDst = (Get-FileHash $VerifyFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($HashSrc -ne $HashDst) {
        throw "VERIFIKATIONSFEHLER: Write=$HashSrc != Readback=$HashDst"
    }

    Write-Host "[T3-Flash] ERFOLG! Flash-Slot 0x5000 verifiziert ($SlotLen Bytes, SHA-256: $HashSrc)." -ForegroundColor Green
    if ($NoReset) {
        Write-Host "[T3-Flash] -NoReset gesetzt. Fuer Neustart: pushd tools\jl-uboot-tool ; ..\..\.venv\Scripts\python.exe jluboottool.py --chip br35 reset ; popd" -ForegroundColor Gray
    }
}
finally {
    Pop-Location
}
