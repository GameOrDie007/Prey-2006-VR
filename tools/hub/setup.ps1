# Prey VR: install, uninstall and log collection, in one script.
#
# It finds YOUR copy of Prey (2006) (every Steam library, GOG, the Windows list of installed
# programs, the usual folders, or a folder you drag in), and makes a VR folder inside it: PreyVR,
# a copy of your game's .pk4 files (about 1.7 GB), and vr_support.pk4 built from them. Your Prey
# itself is not changed. No game data ships with this download.
#
#   Setup.bat           install (or update): this script with no switch
#   Uninstall.bat       -Uninstall: removes everything it installed; your saves and settings are kept
#   Collect logs.bat    -Collect: zips the logs onto your desktop to send us
#
# Safe to run again at any time: running it again is an update.
#
# For tools and hubs (the same switches in every Game Or Die port):
#   -Quiet              no questions, no pause, no Explorer window
#   -GamePath <folder>  install for this Prey folder; several separated by ';'; no searching
#   -Json               print the result as JSON on stdout (nothing else)
#   -Detect             change nothing; report the game and the install
#   -AddToSteam         with -Quiet: add the Steam library entry
#   -CloseSteam         with -Quiet: close Steam to write it (restarted after)
#   -NoSteam, -NoShortcuts   skip the Steam entry / the desktop shortcut
# Exit codes: 0 done, 1 nothing could be installed, 2 error.

[CmdletBinding(PositionalBinding = $false)]
param(
    [switch] $Uninstall,
    [switch] $Collect,
    [switch] $Detect,
    [switch] $Quiet,
    [switch] $Json,
    [string[]] $GamePath,
    [switch] $AddToSteam,
    [switch] $CloseSteam,
    [switch] $NoSteam,
    [switch] $NoShortcuts,
    # Tests: no questions; only the folders given; stand-in Steam, desktop and Documents folders.
    [switch] $NoPrompt,
    [switch] $NoSearch,
    [string] $SteamRoot,
    [string] $DesktopRoot,
    [string] $DocumentsRoot,
    # Folders dropped on Setup.bat arrive as bare arguments; collected here, so none is bound to a
    # switch by position.
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Dropped
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Product = 'Prey VR'
$ProductFile = 'Prey VR'
$Version = '1.0.3'
$Title = 'Prey VR'
$Engine = 'PreyVR.exe'
$EngineArgs = '+set logFile 2'
$Launcher = 'Play PreyVR.bat'
$Flat = 'Play PreyVR flatscreen.bat'
$ShortcutName = 'Prey VR.lnk'

if ($Quiet -or $Json) { $NoPrompt = $true }
# The 1.0 tools print as they go; with -Json, stdout carries the JSON alone.
if ($Json) { function Write-Host { } }
if ($GamePath) { $NoSearch = $true; $Dropped = @($Dropped) + @($GamePath | ForEach-Object { $_ -split ';' } | Where-Object { $_ }) }

# ProviderPath, not Path: on a network share .Path carries a PowerShell provider prefix that .NET
# file calls refuse.
$Here = (Resolve-Path -LiteralPath $PSScriptRoot).ProviderPath
# Run from the unzipped download, the engine sits beside this script in files\; run from an
# installed VR folder (Uninstall, Collect logs), this script sits in VR\tools.
$Package = $null
if (Test-Path -LiteralPath (Join-Path $Here $Engine)) { $Package = $Here }
# The version comes from the version.txt packed beside this script, so it cannot go stale.
if ($Package -and (Test-Path -LiteralPath (Join-Path $Package 'version.txt'))) {
    $first = [string](Get-Content -LiteralPath (Join-Path $Package 'version.txt') -TotalCount 1)
    if ($first -match '(\d+\.\d+(\.\d+)*)\s*$') { $Version = $Matches[1] }
}
$ToolsDir = Join-Path $Here 'tools'
if (-not (Test-Path -LiteralPath (Join-Path $ToolsDir 'prepare-data.ps1'))) { $ToolsDir = $Here }

function PathJoin([string] $a, [string] $b) {
    # Not Join-Path: that asks the drive to exist, and a list of guesses is a list of those.
    try { return [System.IO.Path]::Combine($a, $b) } catch { return '' }
}

# The player's own, never removed: saved games, settings (preyconfig.cfg, written by the game),
# screenshots. autoexec.cfg is ours, replaced by every update.
$KeepPatterns = @('*.cfg')
$NotKept = @('autoexec.cfg')
$KeepDirs = @('savegames', 'screenshots')
# Shipped as a starting point, then the player's: copied only when there is none.
$CopyOnce = @('saves\preybase\preyconfig.cfg')
# The copies of the player's Prey (1.7 GB): an update keeps them rather than copying them again;
# uninstall removes them with the rest. vr_support.pk4 is rebuilt by every install.
$KeepOnUpdate = @('preybase\pak*.pk4', 'preybase\game*.pk4')
# Written by the game while it runs: not part of the install, removed on uninstall.
$EngineOutput = @('qconsole.log', 'PreyVR-vr-error.txt', 'freeze.txt', 'crash.txt', 'PreyVR-report.zip')

# --- the log --------------------------------------------------------------------

$Mode = 'install'
if ($Uninstall) { $Mode = 'uninstall' }
if ($Collect)   { $Mode = 'collect' }
if ($Detect)    { $Mode = 'detect' }
$LogFile = Join-Path ([System.IO.Path]::GetTempPath()) ('prey-vr-' + $Mode + '.txt')
if ($Mode -eq 'install' -and $Package) { $LogFile = Join-Path (Split-Path -Parent $Here) 'setup-log.txt' }

$Result = [ordered]@{ product = $Product; version = $Version; mode = $Mode; result = 'ok' }

function Say([string] $m, [string] $color) {
    if ($Json) { return }	# stdout carries the JSON alone
    if ($color) { Write-Host $m -ForegroundColor $color } else { Write-Host $m }
}
function Log([string] $m, [string] $color) {
    # Line by line from the start, so a failure part-way still leaves a log.
    Add-Content -LiteralPath $LogFile -Value $m -Encoding UTF8
    Say $m $color
}
try { Set-Content -LiteralPath $LogFile -Value ($Product + ' ' + $Version + ' ' + $Mode + ', ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' on ' + $env:COMPUTERNAME) -Encoding UTF8 }
catch { $LogFile = Join-Path ([System.IO.Path]::GetTempPath()) ('prey-vr-' + $Mode + '.txt'); Set-Content -LiteralPath $LogFile -Value ($Product + ' ' + $Mode) -Encoding UTF8 }

function Finish([int] $code) {
    if ($Json) {
        $Result['exitCode'] = $code
        $Result['game'] = Game-Status
        Write-Output ($Result | ConvertTo-Json -Depth 5)
    }
    exit $code
}
trap {
    Log ''
    Log ('ERROR: ' + $_.Exception.Message)
    Log ('  at ' + $_.InvocationInfo.PositionMessage)
    Log ''
    Log 'Setup stopped.  Run "Collect logs.bat" and send us the file it makes.'
    $Result['result'] = 'error'
    $Result['error'] = $_.Exception.Message
    if ($Json) { $Result['exitCode'] = 2; Write-Output ($Result | ConvertTo-Json -Depth 5) }
    exit 2
}

function Banner([string] $what) {
    Say ''
    Say ('  ' + $Product + ' ' + $Version + ': ' + $what) 'Cyan'
    Say ('  ' + ('-' * ($Product.Length + $Version.Length + $what.Length + 3)))
    Say ''
}

function Ask-YesNo([string] $q) {
    # Yes unless the player says no; no questions at all under -Quiet.
    if ($NoPrompt) { return $false }
    try { if ([Console]::IsInputRedirected) { return $false } } catch { }
    $a = Read-Host ('  ' + $q + ' [Y/n]')
    if ($null -eq $a) { return $false }
    return -not ($a.Trim() -match '^(n|no)$')
}

function Desktop {
    if ($DesktopRoot) { return $DesktopRoot }
    return [Environment]::GetFolderPath('Desktop')
}
function Documents {
    if ($DocumentsRoot) { return $DocumentsRoot }
    return [Environment]::GetFolderPath('MyDocuments')
}
# When Windows will not let us write beside the game.
function Fallback-Folder { return (PathJoin (Documents) $ProductFile) }

# --- finding Prey -----------------------------------------------------------------

function Full-Path([string] $p) {
    try { return [System.IO.Path]::GetFullPath($p).TrimEnd('\') } catch { return $p }
}

function True-Case([string] $p) {
    # The folder's name as it is on disk: Steam reports its path in lower case.
    try {
        $d = New-Object System.IO.DirectoryInfo $p
        if (-not $d.Exists) { return $p }
        if (-not $d.Parent) { return $d.FullName.ToUpper() }
        $match = $d.Parent.GetDirectories($d.Name) | Select-Object -First 1
        if (-not $match) { return $p }
        return (PathJoin (True-Case $d.Parent.FullName) $match.Name)
    } catch { return $p }
}

$Roots = New-Object System.Collections.Generic.List[string]
function Add-Root([string] $p) {
    if (-not $p) { return }
    try { $p = [System.IO.Path]::GetFullPath($p.Trim().Trim('"').TrimEnd('\')) } catch { return }
    # A dropped .pk4 means its folder.
    if ($p -match '\.pk4$') { $p = Split-Path -Parent $p }
    $p = True-Case $p
    foreach ($r in $Roots) { if ($r -ieq $p) { return } }
    if (Test-Path -LiteralPath $p -PathType Container) { $Roots.Add($p) }
}

function Steam-Dir {
    if ($SteamRoot) { return $SteamRoot }
    foreach ($k in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        try {
            $v = Get-ItemProperty -LiteralPath $k -ErrorAction Stop
            foreach ($p in @($v.SteamPath, $v.InstallPath)) {
                if ($p -and (Test-Path -LiteralPath ($p -replace '/', '\'))) { return (True-Case ($p -replace '/', '\')) }
            }
        } catch { }
    }
    return $null
}

function Steam-Libraries {
    $libs = New-Object System.Collections.Generic.List[string]
    foreach ($k in @('HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam')) {
        try {
            $v = Get-ItemProperty -LiteralPath $k -ErrorAction Stop
            foreach ($s in @($v.SteamPath, $v.InstallPath)) {
                if (-not $s) { continue }
                $s = $s -replace '/', '\'
                $libs.Add($s)
                $vdf = PathJoin $s 'steamapps\libraryfolders.vdf'
                if (-not (Test-Path -LiteralPath $vdf)) { continue }
                foreach ($line in [System.IO.File]::ReadAllLines($vdf)) {
                    if ($line -match '"path"\s+"([^"]+)"') { $libs.Add(($Matches[1] -replace '\\\\', '\')) }
                }
            }
        } catch { }
    }
    return $libs
}

$Searched = $false
function Search-Roots {
    if ($Searched) { return }
    $script:Searched = $true
    foreach ($d in $Dropped) { if ($d) { Add-Root $d.Trim('"') } }
    # Run from an installed VR folder: the Prey folder is the one above it, and the VR folder
    # itself may be an install made where it was unzipped (1.0).
    if ((Split-Path -Leaf $Here) -ieq 'tools') {
        $vr = Split-Path -Parent $Here
        Add-Root $vr
        Add-Root (Split-Path -Parent $vr)
    }
    Add-Root (Fallback-Folder)
    if ($NoSearch) { return }

    # Steam: app 3970, "Prey 2006". (The 2017 Prey also installs as "Prey"; it has no
    # base\pak000.pk4, which rules it out.)
    foreach ($lib in (Steam-Libraries)) {
        foreach ($n in @('Prey 2006', 'Prey')) { Add-Root (PathJoin $lib ('steamapps\common\' + $n)) }
    }
    # GOG, and whatever installed itself and said where it went: a disc install.
    foreach ($k in @('HKLM:\SOFTWARE\WOW6432Node\GOG.com\Games', 'HKLM:\SOFTWARE\GOG.com\Games')) {
        try {
            foreach ($e in Get-ChildItem -LiteralPath $k -ErrorAction Stop) {
                $p = Get-ItemProperty -LiteralPath $e.PSPath
                if ($p.gameName -match '^Prey' -and $p.path) { Add-Root $p.path }
            }
        } catch { }
    }
    foreach ($k in @('HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
                     'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
                     'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall')) {
        try {
            foreach ($e in Get-ChildItem -LiteralPath $k -ErrorAction Stop) {
                $p = Get-ItemProperty -LiteralPath $e.PSPath
                if ($p.DisplayName -match '^Prey' -and $p.InstallLocation) { Add-Root $p.InstallLocation }
            }
        } catch { }
    }
    # The usual folders, last.
    $drives = @()
    try { $drives = [System.IO.DriveInfo]::GetDrives() | Where-Object { $_.DriveType -eq 'Fixed' -and $_.IsReady } | ForEach-Object { $_.RootDirectory.FullName } } catch { $drives = @('C:\') }
    foreach ($dr in $drives) {
        foreach ($f in @('Program Files (x86)\Prey', 'Program Files\Prey', 'Program Files (x86)\3D Realms\Prey', 'Program Files (x86)\3DRealms\Prey', 'Program Files (x86)\2K Games\Prey', 'GOG Games\Prey', 'Games\Prey', 'Games\Prey 2006')) {
            Add-Root (PathJoin $dr $f)
        }
    }
}

# An install of ours: PreyVR.exe and a manifest, or (1.0) PreyVR.exe and the old Setup's patch
# data, in a folder the player unzipped by hand.
function Is-Ours([string] $p) {
    return [bool]($p -and (Test-Path -LiteralPath (PathJoin $p $Engine)))
}
function Is-Installed([string] $vr) {
    return [bool]($vr -and (Test-Path -LiteralPath (PathJoin $vr 'install-manifest.txt')))
}
function Is-Earlier([string] $p) {
    return [bool]((Is-Ours $p) -and -not (Is-Installed $p) -and (Test-Path -LiteralPath (PathJoin $p 'tools\pk4patch\manifest.json')))
}

# The folder holding pak000.pk4 under a folder: the install, its base, or any folder that merely
# contains one (an extracted archive is <extracted>\Prey\base). Breadth first, bounded.
function Resolve-BaseDir([string] $path) {
    if (-not $path) { return $null }
    try { if (-not (Test-Path -PathType Container $path)) { return $null } } catch { return $null }
    $queue = New-Object System.Collections.Queue
    $queue.Enqueue(@{ Path = $path; Depth = 0 })
    $visited = 0
    while ($queue.Count -gt 0) {
        $node = $queue.Dequeue()
        if (++$visited -gt 400) { break }
        foreach ($try in @((PathJoin $node.Path 'base'), $node.Path)) {
            if (Test-Path -PathType Leaf (PathJoin $try 'pak000.pk4')) { return (True-Case (Get-Item -LiteralPath $try).FullName) }
        }
        if ($node.Depth -ge 3) { continue }
        foreach ($k in @(Get-ChildItem -LiteralPath $node.Path -Directory -ErrorAction SilentlyContinue)) {
            # Our own installs hold copies of the paks: never a source.
            if (Is-Ours $k.FullName) { continue }
            $queue.Enqueue(@{ Path = $k.FullName; Depth = ($node.Depth + 1) })
        }
    }
    return $null
}

# The player's Prey: its base folder, and the game folder that holds it.
$script:GameBase = $null
function Find-Game {
    foreach ($r in $Roots) {
        if (Is-Ours $r) { continue }
        $b = Resolve-BaseDir $r
        if (-not $b) { continue }
        $script:GameBase = $b
        if ((Split-Path -Leaf $b) -ieq 'base') { return (Split-Path -Parent $b) }
        return $b
    }
    return $null
}

# Our install for this Prey: <Prey>\VR, or the Documents folder when Windows would not let us
# write beside the game, or a folder of ours that was handed in directly.
function Find-Install([string] $game) {
    if ($game -and (Is-Installed (PathJoin $game 'VR'))) { return (PathJoin $game 'VR') }
    foreach ($r in $Roots) { if (Is-Installed $r) { return $r } }
    return $null
}
function Find-Earlier {
    foreach ($r in $Roots) { if (Is-Earlier $r) { return $r } }
    return $null
}

# The drag-and-drop fallback.
function Ask-Folder {
    if ($NoPrompt) { return $null }
    try { if ([Console]::IsInputRedirected) { return $null } } catch { }
    Say ''
    Say '  Prey (2006) was not found.' 'Yellow'
    Say '  Its base folder holds pak000.pk4 (Steam: ...\steamapps\common\Prey 2006\base; a disc'
    Say '  install: C:\Program Files (x86)\3DRealms\Prey\base). Steam is not required.'
    while ($true) {
        Say ''
        $line = Read-Host '  Drag your Prey folder onto this window and press Enter'
        if ($null -eq $line) { return $null }
        $line = $line.Trim().Trim('"')
        if (-not $line) { return $null }
        if (-not (Test-Path -LiteralPath $line)) { Say '  That folder does not exist.  Try again, or press Enter to stop.'; continue }
        Add-Root $line
        $found = Find-Game
        if ($found) { return $found }
        Say ('  No pak000.pk4 in or under ' + $line + '.')
    }
}

# --- the Steam library ---------------------------------------------------------------
#
# Steam keeps the games added with "Add a Non-Steam Game" in a binary file,
# userdata\<account>\config\shortcuts.vdf, one per Steam account on this PC. It reads it when it
# starts and writes it when it exits, so it must not be running while this one is changed. Every
# other entry is kept byte for byte.

function Vdf-ReadString([byte[]] $b, [ref] $i) {
    $start = $i.Value
    while ($b[$i.Value] -ne 0) { $i.Value++ }
    $s = [System.Text.Encoding]::UTF8.GetString($b, $start, $i.Value - $start)
    $i.Value++
    return $s
}
function Vdf-ReadMap([byte[]] $b, [ref] $i) {
    $entries = New-Object System.Collections.ArrayList
    while ($true) {
        if ($i.Value -ge $b.Length) { throw 'shortcuts.vdf ends early' }
        $t = $b[$i.Value]; $i.Value++
        if ($t -eq 8) { return ,$entries }
        $k = Vdf-ReadString $b $i
        switch ($t) {
            0 { $v = Vdf-ReadMap $b $i }
            1 { $v = Vdf-ReadString $b $i }
            2 { $v = [BitConverter]::ToInt32($b, $i.Value); $i.Value += 4 }
            7 { $v = [BitConverter]::ToUInt64($b, $i.Value); $i.Value += 8 }
            default { throw ('shortcuts.vdf has a value type this does not know (' + $t + '); left alone') }
        }
        [void]$entries.Add(@{ T = [int]$t; K = $k; V = $v })
    }
}
function Vdf-Write($entries, [System.IO.MemoryStream] $ms) {
    foreach ($e in $entries) {
        $ms.WriteByte([byte]$e.T)
        $kb = [System.Text.Encoding]::UTF8.GetBytes($e.K); $ms.Write($kb, 0, $kb.Length); $ms.WriteByte(0)
        switch ($e.T) {
            0 { Vdf-Write $e.V $ms; $ms.WriteByte(8) }
            1 { $vb = [System.Text.Encoding]::UTF8.GetBytes([string]$e.V); $ms.Write($vb, 0, $vb.Length); $ms.WriteByte(0) }
            2 { $vb = [BitConverter]::GetBytes([int32]$e.V); $ms.Write($vb, 0, 4) }
            7 { $vb = [BitConverter]::GetBytes([uint64]$e.V); $ms.Write($vb, 0, 8) }
        }
    }
}
function Vdf-Get($entries, [string] $k) {
    foreach ($e in $entries) { if ($e.K -ieq $k) { return ,$e.V } }
    return $null
}

# Steam's id for a non-Steam game: CRC-32 of the quoted exe and the name, top bit set. It names
# the library art files. (PowerShell 5.1 reads 0xFFFFFFFF and friends as negative int32s: the
# constants are written out.)
$Crc = $null
$Poly = [uint32]3988292384	# 0xEDB88320
$TopBit = [uint32]2147483648	# 0x80000000
function Crc32([string] $s) {
    if (-not $script:Crc) {
        $script:Crc = New-Object 'uint32[]' 256
        for ($n = 0; $n -lt 256; $n++) {
            $c = [uint32]$n
            for ($k = 0; $k -lt 8; $k++) { if ($c -band 1) { $c = [uint32]($Poly -bxor ($c -shr 1)) } else { $c = [uint32]($c -shr 1) } }
            $script:Crc[$n] = $c
        }
    }
    $c = [uint32]::MaxValue
    foreach ($byte in [System.Text.Encoding]::UTF8.GetBytes($s)) { $c = [uint32]($script:Crc[($c -bxor $byte) -band 0xFF] -bxor ($c -shr 8)) }
    return [uint32]($c -bxor [uint32]::MaxValue)
}
function Shortcut-Id([string] $exeQuoted, [string] $name) { return [uint32]((Crc32 ($exeQuoted + $name)) -bor $TopBit) }

function Steam-Configs {
    $s = Steam-Dir
    if (-not $s) { return @() }
    $u = Join-Path $s 'userdata'
    if (-not (Test-Path -LiteralPath $u)) { return @() }
    return @(Get-ChildItem -LiteralPath $u -Directory | Where-Object { $_.Name -match '^\d+$' -and $_.Name -ne '0' } |
        ForEach-Object { Join-Path $_.FullName 'config' } | Where-Object { Test-Path -LiteralPath $_ })
}

function Steam-Running {
    if ($SteamRoot) { return $false }	# a stand-in folder in a test
    return [bool](Get-Process -Name 'steam' -ErrorAction SilentlyContinue)
}

$SteamWasClosed = $false
function Steam-Close {
    # Steam's own way out, so it writes its files before it goes.
    $exe = Join-Path (Steam-Dir) 'steam.exe'
    Log '  Closing Steam...'
    Start-Process -FilePath $exe -ArgumentList '-shutdown'
    for ($n = 0; $n -lt 60 -and (Steam-Running); $n++) { Start-Sleep -Milliseconds 500 }
    if (Steam-Running) { Log '  Steam did not close.'; return $false }
    $script:SteamWasClosed = $true
    return $true
}
function Steam-Reopen {
    if (-not $SteamWasClosed) { return }
    Start-Process -FilePath (Join-Path (Steam-Dir) 'steam.exe')
    Log '  Steam started again.'
}

# Our entry: named as ours AND starting our engine.
function Is-OurEntry($entry) {
    $name = Vdf-Get $entry.V 'AppName'
    $exe = Vdf-Get $entry.V 'Exe'
    return ($name -eq $Title -and ($exe -like ('*\' + $Engine + '*')))
}

function Read-Shortcuts([string] $file) {
    if (-not (Test-Path -LiteralPath $file)) {
        $root = New-Object System.Collections.ArrayList
        [void]$root.Add(@{ T = 0; K = 'shortcuts'; V = (New-Object System.Collections.ArrayList) })
        return ,$root
    }
    $b = [System.IO.File]::ReadAllBytes($file)
    $i = 0
    $root = Vdf-ReadMap $b ([ref]$i)
    if (-not ($root | Where-Object { $_.K -ieq 'shortcuts' -and $_.T -eq 0 })) { throw ('no shortcuts list in ' + $file + '; left alone') }
    return ,$root
}
function Write-Shortcuts([string] $file, $root) {
    $ms = New-Object System.IO.MemoryStream
    Vdf-Write $root $ms
    $ms.WriteByte(8)
    $backup = $file + '.before-prey-vr'
    if ((Test-Path -LiteralPath $file) -and -not (Test-Path -LiteralPath $backup)) { Copy-Item -LiteralPath $file -Destination $backup }
    [System.IO.File]::WriteAllBytes($file, $ms.ToArray())
}
function Renumber($list) {
    for ($n = 0; $n -lt $list.Count; $n++) { $list[$n].K = [string]$n }
}

function New-ShortcutEntry([string] $vr) {
    # The engine itself, not a launcher: no console window, and Steam sees it running.
    $exe = '"' + (Join-Path $vr $Engine) + '"'
    $id = Shortcut-Id $exe $Title
    $v = New-Object System.Collections.ArrayList
    foreach ($f in @(
        @{ T = 2; K = 'appid'; V = [BitConverter]::ToInt32([BitConverter]::GetBytes($id), 0) },
        @{ T = 1; K = 'AppName'; V = $Title },
        @{ T = 1; K = 'Exe'; V = $exe },
        @{ T = 1; K = 'StartDir'; V = ('"' + $vr + '\"') },
        @{ T = 1; K = 'icon'; V = (Join-Path $vr $Engine) },
        @{ T = 1; K = 'ShortcutPath'; V = '' },
        @{ T = 1; K = 'LaunchOptions'; V = $EngineArgs },
        @{ T = 2; K = 'IsHidden'; V = 0 },
        @{ T = 2; K = 'AllowDesktopConfig'; V = 1 },
        @{ T = 2; K = 'AllowOverlay'; V = 1 },
        @{ T = 2; K = 'OpenVR'; V = 1 },	# a VR game: listed in SteamVR's library too
        @{ T = 2; K = 'Devkit'; V = 0 },
        @{ T = 1; K = 'DevkitGameID'; V = '' },
        @{ T = 2; K = 'DevkitOverrideAppID'; V = 0 },
        @{ T = 2; K = 'LastPlayTime'; V = 0 },
        @{ T = 1; K = 'FlatpakAppID'; V = '' },
        @{ T = 0; K = 'tags'; V = (New-Object System.Collections.ArrayList) })) { [void]$v.Add($f) }
    return @{ Entry = @{ T = 0; K = '0'; V = $v }; Id = $id }
}

function Steam-Add([string] $vr) {
    $configs = Steam-Configs
    if ($configs.Count -eq 0) { return 'no Steam' }
    foreach ($c in $configs) {
        $file = Join-Path $c 'shortcuts.vdf'
        $root = Read-Shortcuts $file
        $list = Vdf-Get $root 'shortcuts'
        foreach ($e in @($list)) { if (Is-OurEntry $e) { [void]$list.Remove($e) } }
        $new = New-ShortcutEntry $vr
        [void]$list.Add($new.Entry)
        Renumber $list
        Write-Shortcuts $file $root
    }
    return 'added'
}

function Steam-HasOurs {
    foreach ($c in Steam-Configs) {
        $file = Join-Path $c 'shortcuts.vdf'
        if (-not (Test-Path -LiteralPath $file)) { continue }
        try { foreach ($e in (Vdf-Get (Read-Shortcuts $file) 'shortcuts')) { if (Is-OurEntry $e) { return $true } } } catch { }
    }
    return $false
}

function Steam-Remove {
    foreach ($c in Steam-Configs) {
        $file = Join-Path $c 'shortcuts.vdf'
        if (-not (Test-Path -LiteralPath $file)) { continue }
        $root = Read-Shortcuts $file
        $list = Vdf-Get $root 'shortcuts'
        $removed = 0
        foreach ($e in @($list)) { if (Is-OurEntry $e) { [void]$list.Remove($e); $removed++ } }
        if ($removed) { Renumber $list; Write-Shortcuts $file $root }
    }
}

function Steam-Ready([bool] $quietAllowed) {
    if (-not (Steam-Running)) { return $true }
    if ($NoPrompt) {
        if ($quietAllowed -and $CloseSteam) { return (Steam-Close) }
        return $false
    }
    if (Ask-YesNo 'Steam is open, and it has to be closed for this.  Close Steam now (it opens again after)?') { return (Steam-Close) }
    return $false
}

# --- the game data -----------------------------------------------------------------
#
# The 1.0 Setup's own steps (tools\prepare-data.ps1), now into the VR folder: your .pk4 files
# copied into preybase, checked against the version this was built with, and vr_support.pk4
# built from them.

function Prepare-Data([string] $dest, [string] $base) {
    Log 'Copying your Prey data (about 1.7 GB the first time) and building vr_support.pk4...'
    & (Join-Path $ToolsDir 'prepare-data.ps1') $dest -PreyDir $base | ForEach-Object { if ($null -ne $_) { Add-Content -LiteralPath $LogFile -Value ('  ' + $_) -Encoding UTF8 } }
    return ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath (PathJoin $dest 'preybase\vr_support.pk4')))
}

# --- install ---------------------------------------------------------------------

function Make-Shortcut([string] $lnk, [string] $target, [string] $arguments, [string] $workdir, [string] $what, [string] $icon) {
    $sh = New-Object -ComObject WScript.Shell
    $s = $sh.CreateShortcut($lnk)
    $s.TargetPath = $target
    $s.Arguments = $arguments
    $s.WorkingDirectory = $workdir
    $s.IconLocation = $icon + ',0'
    $s.Description = $what
    $s.WindowStyle = 1
    $s.Save()
}

function Can-Write([string] $dir) {
    try {
        $probe = Join-Path $dir ('prey-write-test-' + [Guid]::NewGuid().ToString('N') + '.tmp')
        Set-Content -LiteralPath $probe -Value 'x'
        Remove-Item -LiteralPath $probe -Force
        return $true
    } catch { return $false }
}

function Is-Kept([string] $vr, [string] $full) {
    $rel = $full.Substring($vr.Length + 1)
    foreach ($d in $KeepDirs) { if (('\' + $rel) -match ('\\' + [regex]::Escape($d) + '\\')) { return $true } }
    if ($NotKept -contains (Split-Path -Leaf $rel)) { return $false }
    foreach ($p in $KeepPatterns) { if ((Split-Path -Leaf $rel) -like $p) { return $true } }
    return $false
}

function Remove-EmptyUnder([string] $dir) {
    if (-not (Test-Path -LiteralPath $dir)) { return }
    foreach ($d in Get-ChildItem -LiteralPath $dir -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending) {
        if (-not (Get-ChildItem -LiteralPath $d.FullName -Force)) { Remove-Item -LiteralPath $d.FullName -Force }
    }
    if (-not (Get-ChildItem -LiteralPath $dir -Force)) { Remove-Item -LiteralPath $dir -Force }
}

# Everything a manifest lists, and what the engine wrote, out of a VR folder; the player's own stays.
function Remove-Listed([string] $vr, [switch] $Update) {
    $mf = PathJoin $vr 'install-manifest.txt'
    if (-not (Test-Path -LiteralPath $mf)) { return }
    foreach ($rel in (Get-Content -LiteralPath $mf | Where-Object { $_ -and -not $_.StartsWith('#') })) {
        if ($Update -and @($KeepOnUpdate | Where-Object { $rel -like $_ }).Count -gt 0) { continue }
        $p = [System.IO.Path]::GetFullPath((Join-Path $vr $rel))
        # Only inside the VR folder, whatever the file says.
        if (-not $p.StartsWith((Full-Path $vr) + '\', [StringComparison]::OrdinalIgnoreCase)) { continue }
        if ((Test-Path -LiteralPath $p -PathType Leaf) -and -not (Is-Kept $vr $p)) { Remove-Item -LiteralPath $p -Force }
    }
    # The last game log goes to TEMP first, so a report sent after an uninstall or reinstall still has it.
    $lastLog = Join-Path ([System.IO.Path]::GetTempPath()) 'prey-vr-last-qconsole.log'
    $ql = Get-ChildItem -LiteralPath $vr -Recurse -File -Filter 'qconsole.log' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($ql) { try { Copy-Item -LiteralPath $ql.FullName -Destination $lastLog -Force } catch { } }
    foreach ($f in Get-ChildItem -LiteralPath $vr -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $EngineOutput -contains $_.Name }) { Remove-Item -LiteralPath $f.FullName -Force }
}

function Do-Install {
    Banner 'Setup'
    if (-not $Package) { throw ('Run Setup.bat from the folder you unzipped. This folder has no ' + $Engine + ' beside the script: ' + $Here) }
    Search-Roots
    Log ('Looking in ' + $Roots.Count + ' place(s) for Prey.')
    foreach ($r in $Roots) { Add-Content -LiteralPath $LogFile -Value ('  ' + $r) -Encoding UTF8 }
    $game = Find-Game
    # A 1.0 folder the player unzipped by hand and handed in: updated where it is.
    $earlier = Find-Earlier
    if (-not $game -and -not $earlier -and -not (Find-Install $null)) { $game = Ask-Folder }
    $vr = Find-Install $game
    if (-not $vr -and $earlier) { $vr = $earlier; Log ('Updating the earlier version in ' + $earlier) }
    if (-not $vr -and $game) {
        $vr = PathJoin $game 'VR'
        if (-not (Can-Write $game)) {
            $vr = Fallback-Folder
            Log ('Windows does not allow changes to ' + $game + ', so the VR files go in ' + $vr)
        }
    }
    if (-not $vr) {
        Log 'Prey (2006) was not found.  Nothing has been changed.' 'Red'
        Log 'Drag your Prey folder (the one holding base\pak000.pk4) onto Setup.bat to point at it.'
        $Result['result'] = 'game not found'
        Finish 1
    }
    if ($game) { Log ('Prey found: ' + $game + ' (its data in ' + $script:GameBase + ')') 'Green' }
    $Result['gameFolder'] = $game

    New-Item -ItemType Directory -Force -Path $vr | Out-Null
    # An update: what the last install put here goes first, so nothing it no longer ships stays
    # behind. The player's saves and settings are not in the list.
    $isUpdate = Is-Installed $vr
    if ($isUpdate) { Remove-Listed $vr -Update }
    if ($earlier -and $vr -ieq $earlier) {
        # The 1.0 Setup's own files: this install brings its own.
        foreach ($f in @('Setup.bat', 'Update.bat', 'Collect report.bat', 'README.txt', 'tools\setup.ps1', 'tools\update.ps1')) {
            $p = PathJoin $vr $f; if (Test-Path -LiteralPath $p) { Remove-Item -LiteralPath $p -Force }
        }
    }

    Log ('Installing into ' + $vr)
    foreach ($f in Get-ChildItem -LiteralPath $Package -Recurse -File) {
        $rel = $f.FullName.Substring($Package.Length + 1)
        if ($rel -in @('setup.ps1', 'Uninstall.bat', 'Collect logs.bat', 'version.txt')) { continue }
        $dst = PathJoin $vr $rel
        if ($CopyOnce -contains $rel -and (Test-Path -LiteralPath $dst)) { continue }
        [System.IO.Directory]::CreateDirectory((Split-Path -Parent $dst)) | Out-Null
        Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
    }
    # The antivirus can take the engine the moment it lands. Say so now rather than in the headset.
    if (-not (Test-Path -LiteralPath (PathJoin $vr $Engine))) {
        throw ($Engine + ' is not in ' + $vr + ' after copying it. Your antivirus may have quarantined it: restore it and exclude the folder, then run Setup again.')
    }
    # The kit, so Uninstall and Collect logs are there without the download.
    Copy-Item -LiteralPath (Join-Path $Here 'setup.ps1') -Destination (PathJoin $vr 'tools\setup.ps1') -Force
    foreach ($k in @('Uninstall.bat', 'Collect logs.bat')) { Copy-Item -LiteralPath (Join-Path $Here $k) -Destination (PathJoin $vr $k) -Force }
    foreach ($k in @('README.txt', 'LICENSE', 'COPYING.txt')) {
        $s = Join-Path (Split-Path -Parent $Here) $k
        if (Test-Path -LiteralPath $s) { Copy-Item -LiteralPath $s -Destination (PathJoin $vr $k) -Force }
    }

    $base = $(if ($game) { $script:GameBase } else { PathJoin $vr 'preybase' })
    if (-not (Prepare-Data $vr $base)) {
        Log ''
        Log 'Prey''s data could not be copied, or vr_support.pk4 could not be built: the lines above say why.' 'Red'
        Log 'Most often: a disc install without the 1.4 patch (pak005.pk4 and pak006.pk4 missing).'
        Remove-EmptyUnder $vr
        $Result['result'] = 'game not found'
        Finish 1
    }
    # The version first (what the installer reads), then the build's own fingerprints.
    $built = @()
    $shipped = Join-Path $Package 'version.txt'
    if (Test-Path -LiteralPath $shipped) { $built = @(Get-Content -LiteralPath $shipped | Select-Object -Skip 1) }
    Set-Content -LiteralPath (PathJoin $vr 'version.txt') -Value (@($Product + ' ' + $Version) + $built) -Encoding ASCII

    if (-not $NoShortcuts) {
        Make-Shortcut (Join-Path (Desktop) $ShortcutName) (PathJoin $vr $Engine) $EngineArgs $vr 'Prey in VR' (PathJoin $vr $Engine)
        Log ('Desktop shortcut: ' + $ShortcutName)
    }

    # Everything here now is the install, apart from the player's own.
    $manifest = New-Object System.Collections.Generic.List[string]
    foreach ($f in Get-ChildItem -LiteralPath $vr -Recurse -File) {
        if (Is-Kept $vr $f.FullName) { continue }
        if ($EngineOutput -contains $f.Name) { continue }
        $manifest.Add($f.FullName.Substring($vr.Length + 1))
    }
    foreach ($k in @('setup-log.txt', 'install-manifest.txt')) { if ($manifest -notcontains $k) { $manifest.Add($k) } }
    $header = @(
        ('# ' + $Product + ' ' + $Version + ', installed ' + (Get-Date -Format 'yyyy-MM-dd HH:mm')),
        ('# game: ' + $game),
        '# Paths are from this VR folder. Uninstall removes exactly these; your saved games, settings',
        '# (preyconfig.cfg) and screenshots are not in it.')
    Set-Content -LiteralPath (PathJoin $vr 'install-manifest.txt') -Value ($header + ($manifest | Sort-Object)) -Encoding UTF8

    # The Steam library, if Steam is here and the player wants it.
    $Result['steam'] = 'skipped'
    if (-not $NoSteam -and (Steam-Configs).Count -gt 0) {
        $want = $false
        if ($NoPrompt) { $want = [bool]$AddToSteam }
        else { Say ''; $want = Ask-YesNo ('Add ' + $Title + ' to your Steam library (and SteamVR''s)?') }
        if ($want) {
            if (Steam-Ready $true) {
                $Result['steam'] = Steam-Add $vr
                Log 'Added to your Steam library (and SteamVR''s).'
                Steam-Reopen
            } else {
                $Result['steam'] = 'skipped: Steam is open'
                Log 'Not added to Steam: Steam has to be closed.  Run Setup.bat again any time to add it.'
            }
        }
    }

    Log ''
    Log ('Done.  Start your headset streaming (Virtual Desktop or SteamVR) first, then run "' + $Launcher + '".') 'Green'
    Log ('It is in ' + $vr + ', with Uninstall.bat and Collect logs.bat.')
    Copy-Item -LiteralPath $LogFile -Destination (PathJoin $vr 'setup-log.txt') -Force
    $Result['vrFolder'] = $vr
    $Result['result'] = 'installed'
    if (-not $NoPrompt) { try { Start-Process explorer.exe -ArgumentList ('"' + $vr + '"') } catch { } }
}

# --- uninstall -------------------------------------------------------------------

function Do-Uninstall {
    Banner 'Uninstall'
    Search-Roots
    $vr = Find-Install (Find-Game)

    # The Steam library first.
    $Result['steam'] = 'none'
    if (Steam-HasOurs) {
        if (Steam-Ready $true) {
            Steam-Remove
            $Result['steam'] = 'removed'
            Log 'Removed from your Steam library.'
            Steam-Reopen
        } else {
            $Result['steam'] = 'left: Steam is open'
            Log 'Still in your Steam library: Steam has to be closed.  Close it and run Uninstall again.'
        }
    }
    $lnk = Join-Path (Desktop) $ShortcutName
    if (Test-Path -LiteralPath $lnk) { Remove-Item -LiteralPath $lnk -Force; Log ('Removed the desktop shortcut ' + $ShortcutName) }

    if (-not $vr) { Log 'Nothing else to uninstall: no install was found.'; $Result['result'] = 'nothing installed'; return }
    Log ('Removing ' + $Product + ' from ' + $vr)
    Remove-Listed $vr
    Remove-EmptyUnder $vr
    if (Test-Path -LiteralPath $vr) { Log ('  Your saved games and settings are kept in ' + $vr + '.') }
    Log ''
    Log ($Product + ' is uninstalled.  Prey itself is untouched.')
    $Result['result'] = 'uninstalled'
}

# --- collect logs ------------------------------------------------------------------

function Do-Collect {
    Banner 'Collect logs'
    Search-Roots
    $stamp = Get-Date -Format 'yyyyMMdd-HHmm'
    $work = Join-Path ([System.IO.Path]::GetTempPath()) ('prey-vr-logs-' + $stamp)
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force }
    New-Item -ItemType Directory -Path $work | Out-Null

    $game = Find-Game
    $vr = Find-Install $game
    if (-not $vr) { $vr = Find-Earlier }
    if ($vr) {
        foreach ($f in @('install-manifest.txt', 'setup-log.txt', 'version.txt', 'PreyVR-vr-error.txt', 'freeze.txt', 'crash.txt', 'saves\preybase\preyconfig.cfg', 'saves\preybase\autoexec.cfg')) {
            $p = PathJoin $vr $f
            if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination (Join-Path $work ($f -replace '\\', '-')) }
        }
        foreach ($l in Get-ChildItem -LiteralPath (PathJoin $vr 'saves\preybase') -Filter '*.log' -File -ErrorAction SilentlyContinue) { Copy-Item -LiteralPath $l.FullName -Destination $work }
        Log ('Logs from ' + $vr)
    } else { Log 'No install was found; collecting what this PC can say.' }
    # The game log from before the last uninstall or reinstall (Remove-Listed keeps it).
    $lastLog = Join-Path ([System.IO.Path]::GetTempPath()) 'prey-vr-last-qconsole.log'
    if (Test-Path -LiteralPath $lastLog) { Copy-Item -LiteralPath $lastLog -Destination (Join-Path $work 'qconsole-before-reinstall.log') }
    foreach ($m in @('install', 'uninstall')) {
        $p = Join-Path ([System.IO.Path]::GetTempPath()) ('prey-vr-' + $m + '.txt')
        if (Test-Path -LiteralPath $p) { Copy-Item -LiteralPath $p -Destination $work }
    }

    # This PC: what a diagnosis needs, nothing more.
    $pc = @(($Product + ' ' + $Version + ', this PC, ' + (Get-Date -Format 'yyyy-MM-dd HH:mm')), '')
    try { $os = Get-CimInstance Win32_OperatingSystem; $pc += ('Windows: ' + $os.Caption + ' ' + $os.Version) } catch { }
    try { foreach ($v in Get-CimInstance Win32_VideoController) { $pc += ('Graphics: ' + $v.Name + ', driver ' + $v.DriverVersion) } } catch { }
    foreach ($k in @('HKLM:\SOFTWARE\Khronos\OpenXR\1', 'HKCU:\SOFTWARE\Khronos\OpenXR\1')) {
        try { $v = Get-ItemProperty -LiteralPath $k -ErrorAction Stop; if ($v.ActiveRuntime) { $pc += ('OpenXR runtime (' + $k + '): ' + $v.ActiveRuntime) } } catch { }
    }
    foreach ($h in @('HKLM:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit', 'HKCU:\SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit')) {
        try { foreach ($n in (Get-Item -LiteralPath $h -ErrorAction Stop).Property) { $pc += ('OpenXR API layer: ' + $n) } } catch { }
    }
    foreach ($p in @('VirtualDesktop.Streamer', 'vrserver', 'vrcompositor', 'OVRServer_x64')) {
        if (Get-Process -Name $p -ErrorAction SilentlyContinue) { $pc += ('Running: ' + $p) }
    }
    $pc += ''
    $pc += ('Prey folder: ' + $(if ($game) { $game } else { 'not found' }))
    $pc += ('VR folder: ' + $(if ($vr) { $vr } else { 'none' }))
    $pc += ('In the Steam library: ' + (Steam-HasOurs))
    Set-Content -LiteralPath (Join-Path $work 'this-pc.txt') -Value $pc -Encoding UTF8

    $zip = Join-Path (Desktop) ('Prey-VR-logs-' + $stamp + '.zip')
    if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
    Compress-Archive -Path (Join-Path $work '*') -DestinationPath $zip
    Remove-Item -LiteralPath $work -Recurse -Force
    Log ''
    Log ('Made ' + $zip)
    Log 'Send that file to us (Discord or a GitHub issue) with a line on what happened.'
    $Result['zip'] = $zip
    if (-not $NoPrompt) { try { Start-Process explorer.exe -ArgumentList ('/select,"' + $zip + '"') } catch { } }
}

# --- status, for -Detect and -Json -------------------------------------------------

function Game-Status {
    Search-Roots
    $game = Find-Game
    $vr = Find-Install $game
    $earlier = $(if (-not $vr) { Find-Earlier } else { $null })
    $ver = $null
    if ($vr -and (Test-Path -LiteralPath (PathJoin $vr 'version.txt'))) { $ver = [string](Get-Content -LiteralPath (PathJoin $vr 'version.txt') -TotalCount 1) }
    return [pscustomobject][ordered]@{
        key = 'prey'; name = 'Prey'; title = $Title
        # Our install keeps its own copy of the paks: it plays even when Prey has moved.
        found = [bool]($game -or $vr -or $earlier)
        gameFolder = $(if ($game) { $game } elseif ($earlier) { $earlier } else { $vr })
        installed = [bool]$vr
        vrFolder = $vr
        installedVersion = $ver
        launcher = $(if ($vr) { PathJoin $vr $Launcher } else { $null })
        flatLauncher = $(if ($vr) { PathJoin $vr $Flat } else { $null })
        # 1.0, unzipped by hand: the next install updates it where it is.
        olderModFound = [bool]$earlier
    }
}

function Do-Detect {
    Banner 'Find the game'
    Search-Roots
    Log ('Looking in ' + $Roots.Count + ' place(s):')
    foreach ($r in $Roots) { Log ('  ' + $r) }
    $s = Game-Status
    Log ('Prey: ' + $(if ($s.gameFolder) { $s.gameFolder } else { 'not found' }))
    Log ($Product + ': ' + $(if ($s.installed) { $s.installedVersion } elseif ($s.olderModFound) { 'an earlier version, unzipped by hand' } else { 'not installed' }))
    $Result['steamLibrary'] = Steam-HasOurs
}

switch ($Mode) {
    'detect'    { Do-Detect }
    'install'   { Do-Install }
    'uninstall' { Do-Uninstall }
    'collect'   { Do-Collect }
}
Finish 0
