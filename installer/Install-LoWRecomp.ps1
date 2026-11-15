<#
.SYNOPSIS
  LoWRecomp installer - builds WWE Legends of WrestleMania (Xbox 360) as a native
  Windows program from YOUR OWN copy of the game and installs it.

.DESCRIPTION
  1. Checks / installs the build tools (Git, Python, Visual Studio Build Tools with Clang)
  2. Reads your game (disc image .iso or extracted folder with default.xex)
  3. Optional: reads the PS3 version (game folder, e.g. from RPCS3) for PlayStation button symbols
  4. Generates the graphics variants (button symbols, keyboard HUD) from your files
  5. Downloads and builds the ReXGlue SDK, recompiles the game, builds launcher + game
  6. Installs everything into one folder, creates shortcuts and an uninstaller

  Running it again updates an existing installation (settings and save games stay).

.PARAMETER Game        Disc image (.iso) or folder with default.xex
.PARAMETER Ps3         Optional: the PS3 version (NPEB90137): its game folder, e.g. RPCS3's
                       dev_hdd0\game\NPEB90137, for PlayStation button symbols - or a .pkg (only archived)
.PARAMETER InstallDir  Installation folder (asked during a first installation; default %LOCALAPPDATA%\Programs\LoWRecomp)
.PARAMETER Icon        Optional: your own icon for the launcher (.png or .ico)
.PARAMETER DesktopShortcut  Also create a desktop shortcut
.PARAMETER NoPrompt    Do not ask anything (needs -Game for a first installation)
.PARAMETER Reconfigure Ask for installation folder, game, PS3 version and icon again instead of reusing the last answers
.PARAMETER Force       Continue with a game version that is not the supported one
.PARAMETER KeepRoms    Keep untouched copies of the originals in <install>\roms (+8 GB) without asking
.PARAMETER NoRomCopy   Do not keep copies of the originals in <install>\roms, without asking
.PARAMETER BuildDir    Folder for SDK and build files (default: <InstallDir>\build, about 30 GB)
.PARAMETER RexSdk      Use an existing, already built rexglue-sdk checkout instead of building one
.PARAMETER Prepare     For RecompLauncher (prepare step in recomplauncher.json): builds into the folder this
                       installer is in, asks nothing, prints "progress <0-1> <text>" lines and creates no
                       shortcuts, Start menu entries or uninstaller (the launcher manages the folder)
#>
[CmdletBinding()]
param(
  [string]$Game,
  [string]$Ps3,
  [string]$InstallDir,
  [string]$Icon,
  [switch]$DesktopShortcut,
  [switch]$NoPrompt,
  [switch]$Reconfigure,
  [switch]$Force,
  [switch]$KeepRoms,
  [switch]$NoRomCopy,
  [string]$BuildDir,
  [string]$RexSdk,
  [switch]$Prepare
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------
$RepoRoot  = Split-Path -Parent $PSScriptRoot
$Version   = (Get-Content -LiteralPath (Join-Path $RepoRoot 'VERSION') -Raw).Trim()
$RexRepo   = 'https://github.com/rexglue/rexglue-sdk.git'
$RexCommit = '546a082a4356173635f6c40510a41ff53e518b5a'   # SDK state the port was made with
$XexSha1   = 'bd3a0afa0dc0b9f5f69281f103015a5a94362337'   # Xbox 360, PAL/EUR
$Preset    = 'win-amd64-relwithdebinfo'
$AppId     = 'LoWRecomp'
$ProjectUrl = 'https://github.com/NikmanTheEvil/WWE-LoW-Recomp'                 # <- your repository URL
$UninstKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppId"
$German    = (Get-UICulture).TwoLetterISOLanguageName -eq 'de'

function T([string]$en, [string]$de) { if ($German) { return $de } else { return $en } }

# -Prepare (RecompLauncher): no questions, the release folder is the installation
if ($Prepare) { $NoPrompt = $true }

# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------
$script:StepNo = 0
$script:StepCount = 8
# -Prepare: share of the whole run per step, for the "progress" lines
$script:StepSpan = @{ 1 = @(0.00, 0.08); 2 = @(0.08, 0.18); 3 = @(0.18, 0.20); 4 = @(0.20, 0.28)
                      5 = @(0.28, 0.55); 6 = @(0.55, 0.95); 7 = @(0.95, 0.99); 8 = @(0.99, 1.00) }
$script:StepText = ''
function Progress([double]$fraction, [string]$text) {
  if (-not $Prepare) { return }
  $f = [Math]::Min(1.0, [Math]::Max(0.0, $fraction)).ToString('0.000', [Globalization.CultureInfo]::InvariantCulture)
  [Console]::Out.WriteLine("progress $f " + ($text -replace '[\r\n]+', ' '))
  [Console]::Out.Flush()
}
# within a step: creeps towards its end while a long tool runs
function Step-Progress([double]$seconds, [string]$text) {
  $span = $script:StepSpan[$script:StepNo]
  if (-not $span) { return }
  Progress ($span[0] + ($span[1] - $span[0]) * 0.9 * (1 - [Math]::Exp(-$seconds / 900))) $text
}
function Step([string]$en, [string]$de) {
  $script:StepNo++
  $script:StepText = T $en $de
  Write-Host ""
  Write-Host ("[{0}/{1}] {2}" -f $script:StepNo, $script:StepCount, $script:StepText) -ForegroundColor Cyan
  Step-Progress 0 $script:StepText
}
function Info([string]$en, [string]$de) { Write-Host ("      " + (T $en $de)) }
function Ok([string]$en, [string]$de)   { Write-Host ("      " + (T $en $de)) -ForegroundColor Green }
function Warn([string]$en, [string]$de) { Write-Host ("      " + (T $en $de)) -ForegroundColor Yellow }
function Fail([string]$en, [string]$de) { throw (T $en $de) }

function Ask-YesNo([string]$en, [string]$de, [bool]$default) {
  if ($NoPrompt) { return $default }
  $hint = if ($default) { (T '[Y/n]' '[J/n]') } else { (T '[y/N]' '[j/N]') }
  $a = Read-Host ("      " + (T $en $de) + " " + $hint)
  if ([string]::IsNullOrWhiteSpace($a)) { return $default }
  return $a.Trim().ToLower() -in @('y', 'yes', 'j', 'ja')
}

function Quote-Arg([string]$a) {
  if ($a -eq '') { return '""' }
  if ($a -notmatch '[\s"]') { return $a }
  return '"' + ($a -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1') + '"'
}

# Runs a program, writes its output to logs\<Name>.log and shows a live status line.
function Invoke-Tool([string]$Name, [string]$Exe, [string[]]$Arguments, [string]$WorkDir = $PWD.Path,
                     [switch]$AllowFailure) {
  $log = Join-Path $script:LogDir "$Name.log"
  $err = Join-Path $script:LogDir "$Name.err.log"
  $argLine = ($Arguments | ForEach-Object { Quote-Arg $_ }) -join ' '
  if (-not [IO.Path]::IsPathRooted($Exe)) {
    $cmd = Get-Command $Exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd) { $Exe = $cmd.Source }
  }
  $p = Start-Process -FilePath $Exe -ArgumentList $argLine -WorkingDirectory $WorkDir -NoNewWindow -PassThru `
                     -RedirectStandardOutput $log -RedirectStandardError $err
  $null = $p.Handle   # keeps the exit code available (Windows PowerShell quirk)
  $last = ''
  $start = Get-Date
  $lastProgress = [DateTime]::MinValue
  while (-not $p.HasExited) {
    Start-Sleep -Milliseconds 1500
    try {
      # robocopy & co. can return several items here -> always one string
      $line = (@(Get-Content -LiteralPath $log -Tail 1 -ErrorAction Stop) | ForEach-Object { [string]$_ }) -join ' '
      if ($line.Trim() -and $line -ne $last) {
        $last = $line
        $elapsed = (Get-Date) - $start
        $text = ($line -replace '\s+', ' ').Trim()
        if ($text.Length -gt 90) { $text = $text.Substring(0, 87) + '...' }
        if ($Prepare) {
          # one progress line every few seconds instead of the live status line
          if (((Get-Date) - $lastProgress).TotalSeconds -ge 5) {
            $lastProgress = Get-Date
            Step-Progress $elapsed.TotalSeconds ("$($script:StepText): $text")
          }
        } else {
          Write-Host ("`r      [{0:hh\:mm\:ss}] {1}" -f $elapsed, $text.PadRight(90)) -NoNewline
        }
      }
    } catch { }
  }
  $p.WaitForExit()
  if ($last -and -not $Prepare) { Write-Host "" }
  # stderr behind stdout in one log
  if (Test-Path $err) {
    $e = Get-Content -LiteralPath $err -Raw
    if ($e) { Add-Content -LiteralPath $log -Value "`r`n--- stderr ---`r`n$e" }
    Remove-Item -LiteralPath $err -Force
  }
  if ($p.ExitCode -ne 0 -and -not $AllowFailure) {
    Write-Host ""
    Get-Content -LiteralPath $log -Tail 25 | ForEach-Object { Write-Host "      $_" -ForegroundColor DarkGray }
    Fail "$Name failed (exit code $($p.ExitCode)). Full log: $log" "$Name fehlgeschlagen (Exitcode $($p.ExitCode)). Vollständiges Log: $log"
  }
  return $p.ExitCode
}

function Refresh-Path {
  $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
              [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Has-Command([string]$name) { return [bool](Get-Command $name -ErrorAction SilentlyContinue) }

function Winget-Install([string]$id, [string]$override = $null) {
  if (-not (Has-Command 'winget')) {
    Fail "winget is not available - please install $id manually and run the installer again." `
         "winget fehlt - bitte $id selbst installieren und den Installer erneut starten."
  }
  $a = @('install', '--id', $id, '-e', '--source', 'winget', '--accept-package-agreements', '--accept-source-agreements')
  if ($override) { $a += @('--override', $override) }
  Invoke-Tool "winget-$id" 'winget' $a | Out-Null
  Refresh-Path
}

function Pick-File([string]$title, [string]$filter) {
  Add-Type -AssemblyName System.Windows.Forms
  $d = New-Object System.Windows.Forms.OpenFileDialog
  $d.Title = $title
  $d.Filter = $filter
  $d.Multiselect = $false
  if ($d.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { return $d.FileName }
  return $null
}

function Pick-Folder([string]$description, [string]$start) {
  Add-Type -AssemblyName System.Windows.Forms
  $d = New-Object System.Windows.Forms.FolderBrowserDialog
  $d.Description = $description
  $d.ShowNewFolderButton = $true
  if ($start -and (Test-Path $start)) { $d.SelectedPath = $start }
  if ($d.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { return $d.SelectedPath }
  return $null
}

# A folder that already holds other files gets a LoWRecomp subfolder - the
# uninstaller deletes the installation folder, so it must belong to us alone.
function Resolve-InstallDir([string]$dir) {
  $dir = [IO.Path]::GetFullPath($dir.Trim().Trim('"'))
  if (-not $Prepare -and (Test-Path -LiteralPath $dir -PathType Container) -and
      -not (Test-Path (Join-Path $dir 'install.json')) -and
      (Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue | Select-Object -First 1)) {
    $dir = Join-Path $dir 'LoWRecomp'
  }
  $root = [IO.Path]::GetPathRoot($dir)
  $blocked = @($root, $env:WINDIR, $env:ProgramFiles, ${env:ProgramFiles(x86)}, $env:USERPROFILE,
               [Environment]::GetFolderPath('Desktop'), [Environment]::GetFolderPath('MyDocuments')) |
             Where-Object { $_ } | ForEach-Object { $_.TrimEnd('\') }
  if ($blocked -contains $dir.TrimEnd('\')) {
    Fail "Please choose a dedicated folder, not $dir." "Bitte einen eigenen Ordner wählen, nicht $dir."
  }
  # write access? (e.g. "Program Files" needs administrator rights)
  try {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $probe = Join-Path $dir '.write-test'
    Set-Content -LiteralPath $probe -Value 'x'
    Remove-Item -LiteralPath $probe -Force
  } catch {
    Fail "No write access to $dir - please choose another folder (Program Files needs administrator rights)." `
         "Kein Schreibzugriff auf $dir - bitte einen anderen Ordner wählen (Programme braucht Administratorrechte)."
  }
  return $dir
}

function Sha1([string]$path) { return (Get-FileHash -Algorithm SHA1 -LiteralPath $path).Hash.ToLower() }

function Save-State($state) {
  $state | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $InstallDir 'install.json') -Encoding UTF8
}

# ---------------------------------------------------------------------------
# Start
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "  LoWRecomp $Version - WWE Legends of WrestleMania (Xbox 360) for PC" -ForegroundColor White
Write-Host ("  " + (T "Builds the game from your own copy. Nothing of the game is downloaded." `
                      "Baut das Spiel aus deiner eigenen Kopie. Vom Spiel wird nichts heruntergeladen."))

# Installation folder: parameter, otherwise the previous installation (update),
# otherwise ask (Enter = default folder, B = browse, or type a path).
$state = @{}
$prev = if ($Prepare) { $null } else { Get-ItemProperty -Path $UninstKey -ErrorAction SilentlyContinue }
$prevDir = $null
if ($prev -and $prev.InstallLocation -and (Test-Path $prev.InstallLocation)) { $prevDir = $prev.InstallLocation }
# -Prepare: RecompLauncher's folder for this game = the folder this installer came in
if ($Prepare -and -not $InstallDir) { $InstallDir = $RepoRoot }
$defaultDir = Join-Path $env:LOCALAPPDATA 'Programs\LoWRecomp'
try {
  if (-not $InstallDir) {
    if ($prevDir -and -not $Reconfigure) {
      $InstallDir = $prevDir
      Write-Host ("  " + (T "Updating the existing installation (choose another folder with -Reconfigure or -InstallDir)." `
                            "Vorhandene Installation wird aktualisiert (anderer Ordner: -Reconfigure oder -InstallDir)."))
    } elseif ($NoPrompt) {
      $InstallDir = $defaultDir
    } else {
      $suggest = if ($prevDir) { $prevDir } else { $defaultDir }
      Write-Host ""
      Write-Host ("  " + (T "Installation folder (programs, generated graphics and about 30 GB of build files):" `
                            "Installationsordner (Programme, erzeugte Grafiken und ca. 30 GB Build-Dateien):"))
      Write-Host "    $suggest"
      $a = Read-Host ("  " + (T "[Enter] = use it, [B] = browse, or type another path" `
                                "[Enter] = übernehmen, [B] = durchsuchen, oder anderen Pfad eingeben"))
      if ([string]::IsNullOrWhiteSpace($a)) {
        $InstallDir = $suggest
      } elseif ($a.Trim() -ieq 'b') {
        $picked = Pick-Folder (T 'Choose the installation folder for LoWRecomp' 'Installationsordner für LoWRecomp wählen') `
                              (Split-Path -Parent $suggest)
        if (-not $picked) { Fail "No folder selected." "Kein Ordner ausgewählt." }
        $InstallDir = $picked
      } else {
        $InstallDir = $a
      }
    }
  }
  $InstallDir = Resolve-InstallDir $InstallDir
} catch {
  Write-Host ("  " + (T "ERROR: " "FEHLER: ") + $_.Exception.Message) -ForegroundColor Red
  exit 1
}
if ($prevDir -and ([IO.Path]::GetFullPath($prevDir).TrimEnd('\') -ne $InstallDir.TrimEnd('\'))) {
  Warn "The previous installation in $prevDir stays - remove it via Settings > Apps if you no longer need it." `
       "Die bisherige Installation in $prevDir bleibt bestehen - bei Bedarf über Einstellungen > Apps entfernen."
}
$stateFile = Join-Path $InstallDir 'install.json'
if ((Test-Path $stateFile) -and -not $Reconfigure) {
  $j = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
  foreach ($p in $j.PSObject.Properties) { $state[$p.Name] = $p.Value }
}
if (-not $BuildDir) { $BuildDir = Join-Path $InstallDir 'build' }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$script:LogDir = Join-Path $InstallDir 'logs\install'
New-Item -ItemType Directory -Force -Path $BuildDir, $script:LogDir | Out-Null
Write-Host ("  " + (T "Installation folder: " "Installationsordner: ") + $InstallDir)
Write-Host ("  " + (T "Logs: " "Protokolle: ") + $script:LogDir)

# Disk space (SDK + recompiled code need a lot while building)
$drive = Get-PSDrive -Name ($InstallDir.Substring(0, 1)) -ErrorAction SilentlyContinue
if ($drive -and $drive.Free -lt 45GB) {
  Warn ("Only {0:N0} GB free on drive {1}: - build and game need about 45 GB (55 GB with copies of the originals)." -f ($drive.Free / 1GB), $drive.Name) `
       ("Nur {0:N0} GB frei auf Laufwerk {1}: - Build und Spiel brauchen etwa 45 GB (55 GB mit Kopien der Originale)." -f ($drive.Free / 1GB), $drive.Name)
  if (-not (Ask-YesNo "Continue anyway?" "Trotzdem fortfahren?" $false)) {
    Progress 0 (T "Not enough free disk space (about 45 GB needed)." "Nicht genug freier Speicher (etwa 45 GB nötig).")
    exit 1
  }
}

try {
# ===========================================================================
Step "Checking build tools" "Build-Werkzeuge prüfen"
# ===========================================================================
Refresh-Path
if (-not (Has-Command 'git')) {
  Info "Git is missing - installing (winget)..." "Git fehlt - wird installiert (winget)..."
  Winget-Install 'Git.Git'
  $gitDefault = Join-Path $env:ProgramFiles 'Git\cmd'
  if (-not (Has-Command 'git') -and (Test-Path $gitDefault)) { $env:Path += ";$gitDefault" }
  if (-not (Has-Command 'git')) { Fail "Git could not be installed." "Git konnte nicht installiert werden." }
}
Ok ("Git: " + ((& git --version) -join ' ')) ("Git: " + ((& git --version) -join ' '))

function Find-Python {
  $ErrorActionPreference = 'Continue'
  foreach ($c in @(@('py', '-3'), @('python'))) {
    if (-not (Has-Command $c[0])) { continue }
    $exe = $c[0]; $pre = @($c | Select-Object -Skip 1)
    try {
      $v = & $exe @pre -c "import sys; print('%d.%d' % sys.version_info[:2]); print(sys.executable)" 2>$null
      if ($LASTEXITCODE -eq 0 -and $v -and [version]$v[0] -ge [version]'3.10') { return $v[1] }
    } catch { }
  }
  return $null
}
$python = Find-Python
if (-not $python) {
  Info "Python 3.10+ is missing - installing (winget)..." "Python 3.10+ fehlt - wird installiert (winget)..."
  Winget-Install 'Python.Python.3.12'
  $python = Find-Python
  if (-not $python) {
    $cand = Get-ChildItem -Path (Join-Path $env:LOCALAPPDATA 'Programs\Python') -Filter python.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cand) { $python = $cand.FullName }
  }
  if (-not $python) { Fail "Python could not be installed." "Python konnte nicht installiert werden." }
}
Ok "Python: $python" "Python: $python"

# Visual Studio (Build Tools) with C++, Clang, CMake/Ninja and Windows SDK
$vsComponents = @('Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                  'Microsoft.VisualStudio.Component.VC.Llvm.Clang',
                  'Microsoft.VisualStudio.Component.VC.CMake.Project')
$vsAdd = '--add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 ' +
         '--add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset ' +
         '--add Microsoft.VisualStudio.Component.VC.CMake.Project --add Microsoft.VisualStudio.Component.Windows11SDK.26100'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
function Find-VS {
  if (-not (Test-Path $vswhere)) { return $null }
  $p = & $vswhere -latest -products * -version 17.0 -requires $vsComponents -property installationPath
  if ($p) { return @($p)[0] }
  return $null
}
$vs = Find-VS
if (-not $vs) {
  $any = $null
  if (Test-Path $vswhere) { $any = @(& $vswhere -latest -products * -version 17.0 -property installationPath)[0] }
  if ($any) {
    Info "Visual Studio found, adding the missing C++/Clang components (a Windows prompt may appear)..." `
         "Visual Studio gefunden, fehlende C++/Clang-Komponenten werden ergänzt (Windows fragt evtl. nach)..."
    $setup = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
    $a = (@('modify', '--installPath', $any, '--passive', '--norestart') + ($vsAdd -split ' ') |
          ForEach-Object { Quote-Arg $_ }) -join ' '
    # needs administrator rights -> Windows asks (UAC)
    Start-Process -FilePath $setup -ArgumentList $a -Verb RunAs -Wait
  } else {
    Info "Visual Studio Build Tools are missing - installing (about 6 GB, takes a while)..." `
         "Visual Studio Build Tools fehlen - werden installiert (ca. 6 GB, dauert etwas)..."
    Winget-Install 'Microsoft.VisualStudio.2022.BuildTools' "--passive --wait --norestart $vsAdd --includeRecommended"
  }
  $vs = Find-VS
  if (-not $vs) {
    Fail "Visual Studio Build Tools with 'C++ Clang tools' and 'C++ CMake tools' are required. Please add them in the Visual Studio Installer and run this again." `
         "Benötigt werden die Visual Studio Build Tools mit 'C++-Clang-Tools' und 'C++-CMake-Tools'. Bitte im Visual Studio Installer ergänzen und erneut starten."
  }
}
# Developer environment (compiler, Windows SDK, CMake, Ninja) into this session
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$envDump = & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
foreach ($line in $envDump) {
  if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
}
if (-not $env:VCINSTALLDIR) { Fail "Could not load the Visual Studio environment ($vcvars)." "Visual-Studio-Umgebung konnte nicht geladen werden ($vcvars)." }
$llvm = Join-Path $env:VCINSTALLDIR 'Tools\Llvm\x64\bin'
$env:Path = "$llvm;$env:Path"
foreach ($t in @('clang', 'cmake', 'ninja')) {
  if (-not (Has-Command $t)) { Fail "$t not found in the Visual Studio environment ($vs)." "$t fehlt in der Visual-Studio-Umgebung ($vs)." }
}
Ok ("Visual Studio: $vs") ("Visual Studio: $vs")

# Python environment for the asset tools
$venv = Join-Path $BuildDir 'venv'
$py = Join-Path $venv 'Scripts\python.exe'
if (-not (Test-Path $py)) { Invoke-Tool 'python-venv' $python @('-m', 'venv', $venv) | Out-Null }
Invoke-Tool 'python-packages' $py @('-m', 'pip', 'install', '--disable-pip-version-check', '-r',
                                    (Join-Path $RepoRoot 'tools\requirements.txt')) | Out-Null
Ok "Python packages ready" "Python-Pakete bereit"

# ===========================================================================
Step "Game files" "Spieldateien"
# ===========================================================================
# Everything a later update or repair needs is kept inside the installation:
#   <install>\roms\xbox360   untouched copy of your Xbox 360 original (.iso or game folder)
#   <install>\roms\ps3       untouched copy of your PS3 original (game folder or .pkg), optional
#   <install>\game           working copy of the game (extracted / copied; the launcher adds symbol variants)
#   <install>\sources\ps3    the six PS3 files used for PlayStation symbols
$RomsDir = Join-Path $InstallDir 'roms'
$romXbox = Join-Path $RomsDir 'xbox360'
$romPs3 = Join-Path $RomsDir 'ps3'
$GameDir = Join-Path $InstallDir 'game'
function Test-Inside([string]$path, [string]$dir) {
  return ($path.TrimEnd('\') + '\').StartsWith($dir.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)
}
function Has-Content([string]$dir) {
  return [bool](Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue | Select-Object -First 1)
}
# Untouched copy of a file or folder into a roms subfolder; returns the path of the copy
function Copy-Rom([string]$src, [string]$destDir, [string]$logName) {
  if ($src.Length -gt 3) { $src = $src.TrimEnd('\') }
  New-Item -ItemType Directory -Force -Path $destDir | Out-Null
  if (Test-Path -LiteralPath $src -PathType Leaf) {
    $name = Split-Path -Leaf $src
    $rc = Invoke-Tool $logName 'robocopy.exe' @((Split-Path -Parent $src), $destDir, $name, '/J', '/R:1', '/W:1', '/NDL', '/NJH') -AllowFailure
    $dest = Join-Path $destDir $name
  } else {
    $dest = Join-Path $destDir (Split-Path -Leaf $src.TrimEnd('\'))
    $rc = Invoke-Tool $logName 'robocopy.exe' @($src, $dest, '/E', '/R:1', '/W:1', '/NDL', '/NJH', '/NP') -AllowFailure
  }
  if ($rc -ge 8) { Fail "Copying $src failed (robocopy $rc, see $logName.log)." "Kopieren von $src fehlgeschlagen (robocopy $rc, siehe $logName.log)." }
  return $dest
}

$haveGame = Test-Path (Join-Path $GameDir 'default.xex')

# Keep untouched copies of the originals in roms\? (optional, about 8 GB more)
if ($NoRomCopy) { $keepRomCopies = $false }
elseif ($KeepRoms) { $keepRomCopies = $true }
elseif ((Has-Content $romXbox) -or (Has-Content $romPs3)) { $keepRomCopies = $true }
elseif ($state.ContainsKey('KeepRoms') -and -not $Reconfigure) { $keepRomCopies = [bool]$state['KeepRoms'] }
elseif ($NoPrompt) { $keepRomCopies = $false }
else {
  $keepRomCopies = Ask-YesNo "Keep an untouched copy of your originals (.iso / PS3 version) in roms\? (+8 GB)" `
                             "Unveränderte Kopie deiner Originale (.iso / PS3-Fassung) in roms\ ablegen? (+8 GB)" $false
}
$needXboxRom = $keepRomCopies -and -not (Has-Content $romXbox)
if (-not $Game -and -not $Reconfigure) {
  # remembered original (older installations: also the game folder outside the installation)
  foreach ($c in @($state['Game'], $state['GameDir'])) {
    if ($c -and (Test-Path -LiteralPath $c) -and -not (Test-Inside $c $GameDir) -and
        ((-not $haveGame) -or ($needXboxRom -and -not (Test-Inside $c $RomsDir)))) { $Game = $c; break }
  }
}
if (-not $Game -and (-not $haveGame -or $Reconfigure -or $needXboxRom) -and -not $NoPrompt) {
  if ($haveGame -and -not $Reconfigure) {
    Info "For the roms folder: please select your original game (an untouched copy is kept in roms\xbox360)." `
         "Für den roms-Ordner: bitte dein Original auswählen (eine unveränderte Kopie kommt nach roms\xbox360)."
  } else {
    Info "Please select your game: the disc image (.iso) or default.xex in the extracted game folder." `
         "Bitte dein Spiel auswählen: das Disc-Abbild (.iso) oder default.xex im entpackten Spielordner."
  }
  $Game = Pick-File (T 'Select WWE Legends of WrestleMania (Xbox 360)' 'WWE Legends of WrestleMania (Xbox 360) auswählen') `
                    'Xbox 360 (default.xex, *.iso)|default.xex;*.iso|*.*|*.*'
}
if (-not $Game -and -not $haveGame) { Fail "No game selected (-Game)." "Kein Spiel ausgewählt (-Game)." }
if ($Game) {
  $Game = [IO.Path]::GetFullPath($Game)
  if ((Test-Path -LiteralPath $Game -PathType Leaf) -and ([IO.Path]::GetFileName($Game) -ieq 'default.xex')) {
    $Game = Split-Path -Parent $Game
  }
  if (-not (Test-Path -LiteralPath $Game)) { Fail "Not found: $Game" "Nicht gefunden: $Game" }
  if ((Test-Path -LiteralPath $Game -PathType Container) -and -not (Test-Path (Join-Path $Game 'default.xex'))) {
    Fail "default.xex not found in $Game" "default.xex nicht gefunden in $Game"
  }
  # 1) untouched copy into roms\xbox360
  if ($keepRomCopies -and -not (Test-Inside $Game $RomsDir) -and -not (Has-Content $romXbox)) {
    Info "Copying your original into roms\xbox360 (unchanged, about 7 GB)..." "Original wird nach roms\xbox360 kopiert (unverändert, ca. 7 GB)..."
    $Game = Copy-Rom $Game $romXbox 'copy-rom-xbox360'
  }
  # 2) working copy in game\ (only when missing or asked for again)
  if (-not $haveGame -or $Reconfigure) {
    if (Test-Path -LiteralPath $Game -PathType Container) {
      Info "Copying the game into game\ ..." "Spiel wird nach game\ kopiert ..."
      $rc = Invoke-Tool 'copy-game' 'robocopy.exe' @($Game, $GameDir, '/E', '/R:1', '/W:1', '/NDL', '/NJH', '/NP') -AllowFailure
      if ($rc -ge 8) { Fail "Copying the game failed (robocopy $rc, see copy-game.log)." "Kopieren des Spiels fehlgeschlagen (robocopy $rc, siehe copy-game.log)." }
    } else {
      Info "Extracting the disc image into game\ (about 7 GB)..." "Disc-Abbild wird nach game\ entpackt (ca. 7 GB)..."
      Invoke-Tool 'extract-iso' $py @((Join-Path $RepoRoot 'tools\xiso_extract.py'), 'extract', $Game, $GameDir) | Out-Null
    }
  }
}
$xex = Join-Path $GameDir 'default.xex'
if (-not (Test-Path $xex)) { Fail "default.xex not found in $GameDir" "default.xex nicht gefunden in $GameDir" }
$sha = Sha1 $xex
if ($sha -ne $XexSha1) {
  Warn "This default.xex ($sha) is not the supported version (Xbox 360 PAL/EUR, $XexSha1)." `
       "Diese default.xex ($sha) ist nicht die unterstützte Fassung (Xbox 360 PAL/EUR, $XexSha1)."
  if (-not $Force) {
    Fail "Other versions need their own addresses and will not work. Use -Force to try anyway." `
         "Andere Fassungen brauchen eigene Adressen und funktionieren nicht. Mit -Force trotzdem versuchen."
  }
}
$GameOriginal = if ($Game) { $Game } elseif ($state['Game']) { $state['Game'] } else { '' }
Ok "Game: $GameDir" "Spiel: $GameDir"
if (Has-Content $romXbox) { Ok "Original: $romXbox" "Original: $romXbox" }

# ===========================================================================
Step "PlayStation symbols (optional)" "PlayStation-Symbole (optional)"
# ===========================================================================
# Uses the PS3 version's game folder (NPEB90137, e.g. installed in RPCS3:
# dev_hdd0\game\NPEB90137). A .pkg is only kept in roms\ps3 (it is encrypted).
$Ps3Store = Join-Path $InstallDir 'sources\ps3'
$ps3Names = @('fontHD.pac', 'fontSD.pac', 'game2dHD.pac', 'game2dSD.pac', 'LoadHD.pac', 'LoadSD.pac')
function Test-Ps3Store { foreach ($n in $ps3Names) { if (-not (Test-Path (Join-Path $Ps3Store $n))) { return $false } }; return $true }
# SHA-1 of the Xbox file with the same name (the PS3 files have the same names!)
$xboxHashes = @{}
foreach ($n in $ps3Names) {
  $x = Get-ChildItem -LiteralPath $GameDir -Filter $n -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
  $xboxHashes[$n] = if ($x) { Sha1 $x.FullName } else { '' }
}
function Import-Ps3Files([string]$from) {
  New-Item -ItemType Directory -Force -Path $Ps3Store | Out-Null
  $missing = @()
  foreach ($n in $ps3Names) {
    # skip copies of the Xbox file; prefer paths of the PS3 game folder
    $hit = Get-ChildItem -LiteralPath $from -Filter $n -Recurse -File -ErrorAction SilentlyContinue |
           Where-Object { -not (Test-Inside $_.FullName $GameDir) -and -not (Test-Inside $_.FullName $romXbox) } |
           Sort-Object @{ Expression = { if ($_.FullName -match 'NPEB90137|USRDIR') { 0 } else { 1 } } },
                       @{ Expression = { $_.FullName.Length } } |
           Where-Object { (Sha1 $_.FullName) -ne $xboxHashes[$n] } | Select-Object -First 1
    if ($hit) { Copy-Item -LiteralPath $hit.FullName -Destination (Join-Path $Ps3Store $n) -Force }
    else { $missing += $n }
  }
  return $missing
}
# sources\ps3 must hold the PS3 files, not copies of the Xbox ones
if (Test-Path $Ps3Store) {
  $wrong = @($ps3Names | Where-Object { (Test-Path (Join-Path $Ps3Store $_)) -and
                                        ((Sha1 (Join-Path $Ps3Store $_)) -eq $xboxHashes[$_]) })
  if ($wrong.Count -gt 0) {
    Warn "sources\ps3 contained Xbox files instead of PS3 files - removed, please select the PS3 version again." `
         "sources\ps3 enthielt Xbox-Dateien statt PS3-Dateien - entfernt, bitte die PS3-Fassung erneut auswählen."
    Remove-Item -LiteralPath $Ps3Store -Recurse -Force
    $state['Ps3Asked'] = $false
  }
}
# older installation: files from the former PKG extraction
$oldPs3 = Join-Path $BuildDir 'ps3'
if (-not (Test-Ps3Store) -and (Test-Path $oldPs3)) { Import-Ps3Files $oldPs3 | Out-Null }
# PS3 game folder in roms\ps3 (e.g. after deleting sources\)
if (-not (Test-Ps3Store) -and (Has-Content $romPs3)) { Import-Ps3Files $romPs3 | Out-Null }

$needPs3Rom = $keepRomCopies -and -not (Has-Content $romPs3)
$askPs3 = -not $Ps3 -and -not $NoPrompt -and (
            (-not (Test-Ps3Store) -and -not ($state['Ps3Asked'] -and -not $Reconfigure)) -or
            ((Test-Ps3Store) -and $needPs3Rom -and -not $state['Ps3RomAsked']))
if ($askPs3) {
  $q = if (Test-Ps3Store) {
         T "Also keep an untouched copy of your PS3 original (game folder or .pkg) in roms\ps3?" `
           "Auch eine unveränderte Kopie deines PS3-Originals (Spielordner oder .pkg) in roms\ps3 ablegen?"
       } else {
         T "Do you have the PS3 version (NPEB90137, e.g. installed in RPCS3) and want PlayStation button symbols?" `
           "Hast du die PS3-Fassung (NPEB90137, z. B. in RPCS3 installiert) und möchtest PlayStation-Tastensymbole?"
       }
  if (Ask-YesNo $q $q (Test-Ps3Store)) {
    Info "Select PARAM.SFO / EBOOT.BIN in the PS3 game folder, fontHD.pac in a folder with the PS3 files, or the .pkg." `
         "PARAM.SFO / EBOOT.BIN im PS3-Spielordner auswählen, fontHD.pac in einem Ordner mit den PS3-Dateien, oder die .pkg."
    $Ps3 = Pick-File (T 'Select the PS3 version (PARAM.SFO, EBOOT.BIN, fontHD.pac or .pkg)' 'PS3-Fassung auswählen (PARAM.SFO, EBOOT.BIN, fontHD.pac oder .pkg)') `
                     'PS3 (PARAM.SFO, EBOOT.BIN, *.pac, *.pkg)|PARAM.SFO;EBOOT.BIN;*.pac;*.pkg|*.*|*.*'
  }
}
$ps3RomAsked = [bool]$state['Ps3RomAsked'] -or ($askPs3 -and (Test-Ps3Store))
# a file inside the PS3 game folder -> that game folder (NPEB90137\PARAM.SFO,
# NPEB90137\USRDIR\EBOOT.BIN, ...\USRDIR\pac\menu\fontHD.pac); a loose .pac -> its folder
function Resolve-Ps3Folder([string]$path) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or ($path -match '\.pkg$')) { return $path }
  $dir = Split-Path -Parent $path
  for ($d = $dir; $d -and ($d.Length -gt 3); $d = Split-Path -Parent $d) {
    $leaf = Split-Path -Leaf $d
    if ($leaf -ieq 'USRDIR') { return (Split-Path -Parent $d) }
    if ($leaf -match 'NPEB90137') { return $d }
  }
  return $dir
}
if ($Ps3) {
  $Ps3 = [IO.Path]::GetFullPath($Ps3)
  if (-not (Test-Path -LiteralPath $Ps3)) { Fail "Not found: $Ps3" "Nicht gefunden: $Ps3" }
  $Ps3 = Resolve-Ps3Folder $Ps3
  if ($keepRomCopies -and -not (Test-Inside $Ps3 $RomsDir) -and -not (Has-Content $romPs3)) {
    Info "Copying your PS3 original into roms\ps3 (unchanged)..." "PS3-Original wird nach roms\ps3 kopiert (unverändert)..."
    $Ps3 = Copy-Rom $Ps3 $romPs3 'copy-rom-ps3'
  }
  if (Test-Path -LiteralPath $Ps3 -PathType Container) {
    $missing = Import-Ps3Files $Ps3
    if ($missing.Count -gt 0) {
      Warn ("No PS3 version of " + ($missing -join ', ') + " found in this folder (Xbox copies are ignored) - PlayStation symbols are skipped.") `
           ("Keine PS3-Fassung von " + ($missing -join ', ') + " in diesem Ordner gefunden (Xbox-Kopien werden ignoriert) - PlayStation-Symbole werden übersprungen.")
      foreach ($n in $missing) { Remove-Item -LiteralPath (Join-Path $Ps3Store $n) -Force -ErrorAction SilentlyContinue }
    }
  } elseif (-not (Test-Ps3Store)) {
    Warn "A .pkg is encrypted and cannot be read by the installer. For PlayStation symbols install it in RPCS3 and select the game folder (dev_hdd0\game\NPEB90137)." `
         "Eine .pkg ist verschlüsselt und kann vom Installer nicht gelesen werden. Für PlayStation-Symbole in RPCS3 installieren und den Spielordner wählen (dev_hdd0\game\NPEB90137)."
    if (-not $NoPrompt -and (Ask-YesNo "Do you also have a folder with the PS3 files (installed game folder, or fontHD.pac etc.)?" `
                                       "Hast du zusätzlich einen Ordner mit den PS3-Dateien (installierter Spielordner oder fontHD.pac usw.)?" $false)) {
      $f = Pick-Folder (T 'Folder with the PS3 files (e.g. dev_hdd0\game\NPEB90137)' 'Ordner mit den PS3-Dateien (z. B. dev_hdd0\game\NPEB90137)') ''
      if ($f) {
        $missing = Import-Ps3Files $f
        if ($missing.Count -gt 0) {
          Warn ("No PS3 version of " + ($missing -join ', ') + " found in this folder (Xbox copies are ignored) - PlayStation symbols are skipped.") `
               ("Keine PS3-Fassung von " + ($missing -join ', ') + " in diesem Ordner gefunden (Xbox-Kopien werden ignoriert) - PlayStation-Symbole werden übersprungen.")
          foreach ($n in $missing) { Remove-Item -LiteralPath (Join-Path $Ps3Store $n) -Force -ErrorAction SilentlyContinue }
        }
      }
    }
  }
}
$Ps3Dir = $null
if (Test-Ps3Store) {
  $Ps3Dir = $Ps3Store
  Ok "PS3 files: $Ps3Store" "PS3-Dateien: $Ps3Store"
} else {
  Info "Skipped - Xbox and keyboard symbols only." "Übersprungen - nur Xbox- und Tastatursymbole."
}
if (Has-Content $romPs3) { Ok "PS3 original: $romPs3" "PS3-Original: $romPs3" }

$state = @{ Version = $Version; Game = $GameOriginal; GameDir = $GameDir; GameInInstallDir = $true
            Ps3Asked = $true; Ps3RomAsked = $ps3RomAsked; KeepRoms = $keepRomCopies
            Icon = $(if ($Icon) { $Icon } elseif ($state['Icon']) { $state['Icon'] } else { '' }) }
Save-State $state

# ===========================================================================
Step "Graphics variants from your game files" "Grafikvarianten aus deinen Spieldateien"
# ===========================================================================
$assetArgs = @((Join-Path $RepoRoot 'tools\low_assets.py'), 'build', '--game', $GameDir, '--out', $InstallDir)
if ($Ps3Dir) { $assetArgs += @('--ps3', $Ps3Dir) }
$ps3Key = if ($Ps3Dir) { ($ps3Names | ForEach-Object { $f = Get-Item (Join-Path $Ps3Dir $_); "$($f.Length)@$($f.LastWriteTimeUtc.Ticks)" }) -join ',' } else { '' }
$assetKey = "$Version|$sha|$ps3Key|" + (Sha1 (Join-Path $RepoRoot 'tools\data\kbhud_recipe.json'))
$assetStamp = Join-Path $BuildDir 'assets.stamp'
$haveAssets = (Test-Path (Join-Path $InstallDir 'kbhud\game2dHK.pac')) -and (Test-Path $assetStamp) -and
              ((Get-Content -LiteralPath $assetStamp -Raw).Trim() -eq $assetKey)
if ($haveAssets) {
  Info "Already up to date." "Schon aktuell."
} else {
  Info "Button symbols, keyboard HUD$(if ($Ps3Dir) { ', PS3 HUD' }) - a few minutes..." `
       "Tastensymbole, Tastatur-HUD$(if ($Ps3Dir) { ', PS3-HUD' }) - ein paar Minuten..."
  Invoke-Tool 'assets' $py $assetArgs | Out-Null
  Set-Content -LiteralPath $assetStamp -Value $assetKey
}
Ok "Graphics ready" "Grafiken bereit"

# ===========================================================================
Step "ReXGlue SDK (first time: 20-60 minutes)" "ReXGlue-SDK (beim ersten Mal 20-60 Minuten)"
# ===========================================================================
$sdk = if ($RexSdk) { [IO.Path]::GetFullPath($RexSdk) } else { Join-Path $BuildDir 'rexglue-sdk' }
$sdkInstall = Join-Path $sdk 'out\install\win-amd64'
$rex = Join-Path $sdkInstall 'bin\rexglue.exe'
$sdkStamp = Join-Path $sdk 'out\lowrecomp-sdk.stamp'
$sdkOk = (Test-Path $rex) -and (Test-Path $sdkStamp) -and ((Get-Content -LiteralPath $sdkStamp -Raw).Trim() -eq $RexCommit)
if ($RexSdk -and (Test-Path $rex) -and -not $sdkOk) {
  $head = try { (& git -C $sdk rev-parse HEAD 2>$null) } catch { '' }
  if ($head -eq $RexCommit) { $sdkOk = $true; Set-Content -LiteralPath $sdkStamp -Value $RexCommit }
  else { Warn "Existing SDK is at $head, expected $RexCommit - it will be rebuilt." "Vorhandenes SDK steht auf $head, erwartet $RexCommit - es wird neu gebaut." }
}
if ($sdkOk) {
  Info "Already built." "Schon gebaut."
} else {
  if (-not (Test-Path (Join-Path $sdk '.git'))) {
    Invoke-Tool 'sdk-clone' 'git' @('-c', 'core.longpaths=true', 'clone', $RexRepo, $sdk) | Out-Null
  }
  Invoke-Tool 'sdk-config' 'git' @('-C', $sdk, 'config', 'core.longpaths', 'true') | Out-Null
  Invoke-Tool 'sdk-fetch' 'git' @('-C', $sdk, 'fetch', 'origin') | Out-Null
  Invoke-Tool 'sdk-checkout' 'git' @('-C', $sdk, 'checkout', '--force', $RexCommit) | Out-Null
  Invoke-Tool 'sdk-submodules' 'git' @('-C', $sdk, 'submodule', 'update', '--init', '--recursive') | Out-Null
  Invoke-Tool 'sdk-configure' 'cmake' @('--preset', 'win-amd64') $sdk | Out-Null
  Invoke-Tool 'sdk-build' 'cmake' @('--build', 'out/build/win-amd64', '--config', 'Release', '--target', 'install') $sdk | Out-Null
  if (-not (Test-Path $rex)) { Fail "rexglue.exe was not built." "rexglue.exe wurde nicht gebaut." }
  Set-Content -LiteralPath $sdkStamp -Value $RexCommit
}
Ok "SDK ready" "SDK bereit"

# ===========================================================================
Step "Recompiling and building the game (first time: 20-60 minutes)" "Spiel übersetzen und bauen (beim ersten Mal 20-60 Minuten)"
# ===========================================================================
$proj = Join-Path $BuildDir 'project'
New-Item -ItemType Directory -Force -Path $proj | Out-Null
# Project sources into the build folder (generated code and build output stay)
& robocopy.exe (Join-Path $RepoRoot 'project') $proj /MIR /XD default out /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { Fail "Copying the project failed (robocopy $LASTEXITCODE)." "Projekt kopieren fehlgeschlagen (robocopy $LASTEXITCODE)." }

# Own icon (optional)
$iconSrc = $state['Icon']
$ico = Join-Path $proj 'resources\lowrecomp.ico'
if ($iconSrc) {
  if (-not (Test-Path $iconSrc)) {
    Warn "Icon not found ($iconSrc) - using the default icon." "Symbol nicht gefunden ($iconSrc) - Standardsymbol wird verwendet."
  } elseif ($iconSrc -match '\.ico$') {
    Copy-Item -LiteralPath $iconSrc -Destination $ico -Force
  } else {
    Invoke-Tool 'icon' $py @((Join-Path $RepoRoot 'tools\low_assets.py'), 'icon', '--png', $iconSrc, '--out', $ico) | Out-Null
  }
}

# Manifest with the paths of your game (TOML basic strings, forward slashes)
function Toml-Str([string]$v) { return '"' + (($v -replace '\\', '\\') -replace '"', '\"') + '"' }
$g = $GameDir -replace '\\', '/'
$manifest = @"
# Written by the LoWRecomp installer
[project]
name = "lowrecomp"
sdk_version = "0.10.1"
game_root = $(Toml-Str $g)

[entrypoint]
file_path = $(Toml-Str "$g/default.xex")
out_directory_path = "generated/default"
includes = ['config/default_codegen.toml']
"@
[IO.File]::WriteAllText((Join-Path $proj 'lowrecomp_manifest.toml'), $manifest, (New-Object Text.UTF8Encoding($false)))

Info "Recompiling the game code..." "Spielcode wird übersetzt..."
Invoke-Tool 'codegen' $rex @('--log-level', 'info', 'codegen', 'lowrecomp_manifest.toml') $proj | Out-Null
if (Select-String -LiteralPath (Join-Path $script:LogDir 'codegen.log') -Pattern 'Unresolved' -Quiet) {
  Fail "The recompiler reports unresolved jumps (see codegen.log)." "Der Recompiler meldet unaufgelöste Sprünge (siehe codegen.log)."
}
Info "Compiling (uses all CPU cores)..." "Kompilieren (nutzt alle CPU-Kerne)..."
$sdkCmake = (Join-Path $sdkInstall 'lib\cmake\rexglue') -replace '\\', '/'
Invoke-Tool 'configure' 'cmake' @('--preset', $Preset,
    "-Drexglue_DIR=$sdkCmake",
    ("-DCMAKE_PREFIX_PATH=" + ($sdkInstall -replace '\\', '/')),
    ("-DLOW_IMGUI_DIR=" + ((Join-Path $sdk 'thirdparty\imgui') -replace '\\', '/')),
    ("-DLOWRECOMP_ICON=" + ($ico -replace '\\', '/'))) $proj | Out-Null
Invoke-Tool 'build' 'cmake' @('--build', '--preset', $Preset) $proj | Out-Null
$bin = Join-Path $proj "out\build\$Preset"
foreach ($f in @('lowrecomp.exe', 'LoWLauncher.exe')) {
  if (-not (Test-Path (Join-Path $bin $f))) { Fail "$f was not built." "$f wurde nicht gebaut." }
}
Ok "Game built" "Spiel gebaut"

# ===========================================================================
Step "Installing" "Installieren"
# ===========================================================================
Get-ChildItem -LiteralPath $bin -File | Where-Object { $_.Extension -in '.exe', '.dll', '.pdb' -or $_.Name -eq 'gamecontrollerdb.txt' } |
  ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $InstallDir -Force }
# Built in place (-Prepare: the release folder is the installation) -> the
# installer, docs and uninstaller are already here and the launcher removes it.
$inPlace = $RepoRoot.TrimEnd('\') -ieq $InstallDir.TrimEnd('\')
if (-not $inPlace) {
  Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Uninstall-LoWRecomp.ps1') -Destination (Join-Path $InstallDir 'uninstall.ps1') -Force
  foreach ($doc in @('README.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md')) {
    $src = Join-Path $RepoRoot $doc
    if (Test-Path $src) { Copy-Item -LiteralPath $src -Destination $InstallDir -Force }
  }
  $docsDir = Join-Path $InstallDir 'docs'
  New-Item -ItemType Directory -Force -Path $docsDir | Out-Null
  Copy-Item -Path (Join-Path $RepoRoot 'docs\*') -Destination $docsDir -Recurse -Force
}

# Copy of this installer version (sources\installer): repairs and rebuilds work
# without the downloaded repository (Start menu: "Update or repair").
$srcCopy = Join-Path $InstallDir 'sources\installer'
if (-not $inPlace -and $RepoRoot.TrimEnd('\') -ne $srcCopy.TrimEnd('\')) {
  & robocopy.exe $RepoRoot $srcCopy /MIR /XD .git __pycache__ build out /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { Warn "Could not copy the installer to $srcCopy." "Installer konnte nicht nach $srcCopy kopiert werden." }
}

# Configuration: keep the player's file, only (re)set the game folder
$cfg = Join-Path $InstallDir 'lowrecomp.toml'
if (-not (Test-Path $cfg)) {
  $text = Get-Content -LiteralPath (Join-Path $RepoRoot 'project\config\lowrecomp.toml') -Raw -Encoding UTF8
  $lang = switch ((Get-UICulture).TwoLetterISOLanguageName) { 'de' { 3 } 'fr' { 4 } 'es' { 5 } 'it' { 6 } default { 1 } }
  $text = $text -replace '(?m)^user_language = \d+', "user_language = $lang"
} else {
  $text = Get-Content -LiteralPath $cfg -Raw -Encoding UTF8
}
$line = "game_data_root = " + (Toml-Str $GameDir)
if ($text -match '(?m)^game_data_root\s*=') { $text = $text -replace '(?m)^game_data_root\s*=[^\r\n]*', $line.Replace('$', '$$') }
else { $text = "$line`r`n" + $text }
[IO.File]::WriteAllText($cfg, $text, (New-Object Text.UTF8Encoding($false)))
Ok "Files installed" "Dateien installiert"

# -Prepare: RecompLauncher starts, updates and removes the game itself
if ($Prepare) {
  $state['InstalledAt'] = (Get-Date).ToString('s')
  $state['Prepared'] = $true
  Save-State $state
  Progress 1 (T "Ready to play" "Bereit zum Spielen")
  exit 0
}

# ===========================================================================
Step "Shortcuts and uninstaller" "Verknüpfungen und Deinstallation"
# ===========================================================================
$launcher = Join-Path $InstallDir 'LoWLauncher.exe'
$shell = New-Object -ComObject WScript.Shell
$menuDir = Join-Path ([Environment]::GetFolderPath('Programs')) 'LoWRecomp'
New-Item -ItemType Directory -Force -Path $menuDir | Out-Null
function New-Shortcut([string]$path, [string]$target, [string]$arguments, [string]$desc, [string]$icon) {
  $l = $shell.CreateShortcut($path)
  $l.TargetPath = $target
  if ($arguments) { $l.Arguments = $arguments }
  $l.WorkingDirectory = $InstallDir
  $l.Description = $desc
  if ($icon) { $l.IconLocation = $icon }
  $l.Save()
}
$title = 'WWE Legends of WrestleMania (LoWRecomp)'
New-Shortcut (Join-Path $menuDir "$title.lnk") $launcher '' $title "$launcher,0"
$uninstCmd = "-NoProfile -ExecutionPolicy Bypass -File `"$(Join-Path $InstallDir 'uninstall.ps1')`""
New-Shortcut (Join-Path $menuDir ((T 'Uninstall LoWRecomp' 'LoWRecomp deinstallieren') + '.lnk')) 'powershell.exe' $uninstCmd 'LoWRecomp' "$launcher,0"
$repairBat = Join-Path $srcCopy 'install.bat'
if (Test-Path $repairBat) {
  New-Shortcut (Join-Path $menuDir ((T 'Update or repair LoWRecomp' 'LoWRecomp aktualisieren oder reparieren') + '.lnk')) `
               'cmd.exe' "/c `"`"$repairBat`"`"" 'LoWRecomp' "$launcher,0"
}
$desktopLnk = Join-Path ([Environment]::GetFolderPath('Desktop')) "$title.lnk"
if ($DesktopShortcut -or (Test-Path $desktopLnk) -or
    (Ask-YesNo "Create a desktop shortcut?" "Verknüpfung auf dem Desktop anlegen?" $true)) {
  New-Shortcut $desktopLnk $launcher '' $title "$launcher,0"
}

$size = [int]((Get-ChildItem -LiteralPath $InstallDir -Recurse -File -ErrorAction SilentlyContinue |
                Measure-Object -Property Length -Sum).Sum / 1KB)
New-Item -Path $UninstKey -Force | Out-Null
$props = @{
  DisplayName = $title; DisplayVersion = $Version; Publisher = 'LoWRecomp project'
  InstallLocation = $InstallDir; DisplayIcon = "$launcher,0"
  UninstallString = "powershell.exe $uninstCmd"; QuietUninstallString = "powershell.exe $uninstCmd -Quiet"
  NoModify = 1; NoRepair = 1; EstimatedSize = $size; URLInfoAbout = $ProjectUrl
}
foreach ($k in $props.Keys) {
  $type = if ($props[$k] -is [int]) { 'DWord' } else { 'String' }
  New-ItemProperty -Path $UninstKey -Name $k -Value $props[$k] -PropertyType $type -Force | Out-Null
}
Ok "Start menu entry, uninstaller registered (Settings > Apps)" "Startmenü-Eintrag, Deinstallation eingetragen (Einstellungen > Apps)"

$state['InstalledAt'] = (Get-Date).ToString('s')
Save-State $state

Write-Host ""
Write-Host ("  " + (T "Done! Start the game with the launcher:" "Fertig! Gestartet wird das Spiel über den Launcher:")) -ForegroundColor Green
Write-Host "  $launcher"
Write-Host ("  " + (T "Save games: Documents\lowrecomp   |   roms, game, sources = everything needed for updates (keep)" `
                      "Spielstände: Dokumente\lowrecomp   |   roms, game, sources = alles für Updates (behalten)"))
Write-Host ("  " + (T "The folder 'build' (about 30 GB) can be deleted to save space - the next update then takes longer." `
                      "Der Ordner 'build' (ca. 30 GB) kann gelöscht werden - das nächste Update dauert dann länger."))
if (Ask-YesNo "Start the launcher now?" "Launcher jetzt starten?" $true) { Start-Process -FilePath $launcher -WorkingDirectory $InstallDir }
exit 0

} catch {
  Write-Host ""
  Write-Host ("  " + (T "ERROR: " "FEHLER: ") + $_.Exception.Message) -ForegroundColor Red
  Write-Host ("  " + (T "Logs: " "Protokolle: ") + $script:LogDir)
  Progress 0 ((T "ERROR: " "FEHLER: ") + $_.Exception.Message)
  exit 1
}
