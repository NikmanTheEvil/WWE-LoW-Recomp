<#
.SYNOPSIS
  LoWRecomp - removes the installation (programs, generated files, shortcuts,
  Apps entry). Your game files stay; save games stay unless you choose otherwise.
#>
param([switch]$Quiet)

$ErrorActionPreference = 'Stop'
$German = (Get-UICulture).TwoLetterISOLanguageName -eq 'de'
function T([string]$en, [string]$de) { if ($German) { return $de } else { return $en } }
function Ask([string]$en, [string]$de, [bool]$default) {
  if ($Quiet) { return $default }
  $hint = if ($default) { (T '[Y/n]' '[J/n]') } else { (T '[y/N]' '[j/N]') }
  $a = Read-Host ((T $en $de) + " " + $hint)
  if ([string]::IsNullOrWhiteSpace($a)) { return $default }
  return $a.Trim().ToLower() -in @('y', 'yes', 'j', 'ja')
}

$InstallDir = $PSScriptRoot
# Only ever run from an installation folder (the installer copies this file there as uninstall.ps1)
if (-not ((Test-Path (Join-Path $InstallDir 'install.json')) -and (Test-Path (Join-Path $InstallDir 'LoWLauncher.exe')))) {
  Write-Host (T "This is not a LoWRecomp installation folder: $InstallDir" "Das ist kein LoWRecomp-Installationsordner: $InstallDir") -ForegroundColor Red
  exit 1
}
$UninstKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\LoWRecomp'
Write-Host ""
Write-Host ("  " + (T "Uninstall LoWRecomp from " "LoWRecomp deinstallieren aus ") + $InstallDir) -ForegroundColor White
Write-Host ("  " + (T "This removes the programs, the game copy (game\), sources\ and the build files." `
                      "Entfernt werden Programme, die Spielkopie (game\), sources\ und die Build-Dateien."))
if (-not (Ask "Continue?" "Fortfahren?" $true)) { exit 0 }

if (Get-Process -Name 'lowrecomp', 'LoWLauncher' -ErrorAction SilentlyContinue) {
  Write-Host ("  " + (T "Please close the game and the launcher first." "Bitte zuerst Spiel und Launcher schließen.")) -ForegroundColor Yellow
  if (-not $Quiet) { Read-Host (T "Press Enter when done" "Danach Enter drücken") | Out-Null }
}

$state = $null
$stateFile = Join-Path $InstallDir 'install.json'
if (Test-Path $stateFile) { $state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json }

# Variant files the launcher wrote into YOUR game folder (originals stay untouched)
if ($state -and $state.GameDir -and -not ($state.GameExtracted -or $state.GameInInstallDir) -and (Test-Path $state.GameDir)) {
  $names = @()
  foreach ($tag in 'P', 'K') {
    foreach ($q in 'H', 'S') {
      $names += "pac\menu\font$q$tag.pac", "pac\game2d$q$tag.pac", "pac\menu\Load$q$tag.pac"
    }
  }
  foreach ($n in $names) {
    foreach ($f in @((Join-Path $state.GameDir $n), (Join-Path $state.GameDir "$n.tmp"))) {
      if (Test-Path -LiteralPath $f) { Remove-Item -LiteralPath $f -Force }
    }
  }
  Write-Host ("  " + (T "Removed the generated symbol files from the game folder." "Erzeugte Symbol-Dateien aus dem Spielordner entfernt."))
}

# Shortcuts and Apps entry
$menuDir = Join-Path ([Environment]::GetFolderPath('Programs')) 'LoWRecomp'
if (Test-Path $menuDir) { Remove-Item -LiteralPath $menuDir -Recurse -Force }
$desktopLnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'WWE Legends of WrestleMania (LoWRecomp).lnk'
if (Test-Path $desktopLnk) { Remove-Item -LiteralPath $desktopLnk -Force }
if (Test-Path $UninstKey) { Remove-Item -Path $UninstKey -Recurse -Force }

# Save games (ReXGlue: Documents\lowrecomp, unless user_data_root was changed)
$saves = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'lowrecomp'
if ((Test-Path $saves) -and (Ask "Also delete your save games ($saves)?" "Auch die Spielstände löschen ($saves)?" $false)) {
  Remove-Item -LiteralPath $saves -Recurse -Force
}

# roms\ = untouched copies of the originals: kept unless asked otherwise
$roms = Join-Path $InstallDir 'roms'
$keepRoms = (Test-Path $roms) -and [bool](Get-ChildItem -LiteralPath $roms -Force | Select-Object -First 1) -and
            -not (Ask "Also delete roms\ (your untouched copies of the original game files)?" `
                      "Auch roms\ löschen (deine unveränderten Kopien der Original-Spieldateien)?" $false)

# dev\ = your own development files (notes, test data): always kept if present
$dev = Join-Path $InstallDir 'dev'
$keepDev = (Test-Path $dev) -and [bool](Get-ChildItem -LiteralPath $dev -Force | Select-Object -First 1)
$keepNames = @()
if ($keepRoms) { $keepNames += 'roms' }
if ($keepDev) { $keepNames += 'dev' }

# Program folder (game copy in game\, sources\, build files - and roms\ if wanted)
Set-Location $env:TEMP
$ok = $true
try {
  if ($keepNames.Count -gt 0) {
    Get-ChildItem -LiteralPath $InstallDir -Force | Where-Object { $keepNames -notcontains $_.Name } |
      ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
  } else {
    Remove-Item -LiteralPath $InstallDir -Recurse -Force
  }
} catch { $ok = $false }
if ($ok) {
  Write-Host ("  " + (T "LoWRecomp has been removed." "LoWRecomp wurde entfernt.")) -ForegroundColor Green
  if ($keepRoms) { Write-Host ("  " + (T "Your originals are still in " "Deine Originale liegen weiterhin in ") + $roms) }
  if ($keepDev) { Write-Host ("  " + (T "Your development files are still in " "Deine Entwicklungsdateien liegen weiterhin in ") + $dev) }
} else {
  Write-Host ("  " + (T "Some files could not be deleted (in use?): " "Einige Dateien konnten nicht gelöscht werden (in Benutzung?): ") + $InstallDir) -ForegroundColor Yellow
}
if (-not $Quiet) { Start-Sleep -Seconds 3 }
