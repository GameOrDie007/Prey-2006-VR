# Put the Prey game data next to PreyVR, from the copy already on this machine.
#
# Nothing is downloaded and nothing is installed. The retail .pk4 files belong
# to their publisher; this only copies the ones you already own into the folder
# the port reads, alongside the vr_support.pk4 the release ships.
#
# It looks for Prey in your Steam libraries and in the uninstall entries, and if
# it cannot find it, it asks you to drag the files or the folder onto the window.
#
#   powershell -ExecutionPolicy Bypass -File tools\setup.ps1 [install dir]
#
#     -PreyDir <path>   the Prey install or its base folder, if you know it
#     -Ask              skip the search and ask for the files, even if Prey is
#                       installed - for choosing between two copies
#     -Verify           also SHA256 every copied file (slow, ~1.7 GB)

param(
    [string]$InstallDir = ".",
    [string]$PreyDir = "",
    [switch]$Ask,
    [switch]$Verify
)

$ErrorActionPreference = 'Stop'

# A path handed in as "D:\Games\Prey\" arrives with its closing quote escaped
# by the backslash, so the argument is mangled before this script sees it.
# Trimming here rescues the survivable half of that.
$PreyDir = $PreyDir.Trim().TrimEnd('\', '"')

# The files a Prey 1.4 install has, with the size each one is in the version
# this port was built and tested against. The sizes are a version check, not a
# gate: a different release or language will differ and will still very likely
# work, so a mismatch is reported and never refused.
$Tested = [ordered]@{
    'game00.pk4' = 1697374
    'game01.pk4' = 2722496
    'game02.pk4' = 5885875
    'game03.pk4' = 1683408
    'pak000.pk4' = 310998499
    'pak001.pk4' = 302809611
    'pak002.pk4' = 476183237
    'pak003.pk4' = 307817095
    'pak004.pk4' = 286719959
    'pak005.pk4' = 235058
    'pak006.pk4' = 18855657
}

# Without these five there is no game to run - they are the world, the models
# and the sound. The rest are patch and script paks: their absence is worth
# saying out loud but is not a reason to stop.
$Core = @('pak000.pk4', 'pak001.pk4', 'pak002.pk4', 'pak003.pk4', 'pak004.pk4')

# pak005 and pak006 are the 1.4 patch. They are not optional here: three of the
# files vr_support.pk4 is built from live inside them, so the build cannot
# finish without them. Named separately so the message can say "the 1.4 patch"
# instead of reciting filenames at somebody with no way to know what they are.
$Patch = @('pak005.pk4', 'pak006.pk4')

# game00-03 are binary.conf plus gamex86.dll, gamex86.so and game.so.bundle -
# the 32-bit Windows, Linux and macOS game binaries. This port ships its own
# 64-bit gamex86_64.dll and never loads them. Copied when they happen to be
# there, so an install looks like an install, but never asked for.
$Extra = @('game00.pk4', 'game01.pk4', 'game02.pk4', 'game03.pk4')

# What a person is actually asked to supply.
$Needed = $Core + $Patch

function PathJoin([string]$a, [string]$b) {
    return [System.IO.Path]::Combine($a, $b)
}

function Fmt-Size([long]$bytes) {
    # pak020.pk4 is 19 KB and pak002.pk4 is 454 MB. One unit cannot show both:
    # in megabytes the small ones all read "0 MB", which looks like a failed
    # copy rather than a small file.
    if ($bytes -ge 1GB) { return ("{0,6:N1} GB" -f ($bytes / 1GB)) }
    if ($bytes -ge 1MB) { return ("{0,6:N0} MB" -f ($bytes / 1MB)) }
    if ($bytes -ge 1KB) { return ("{0,6:N0} KB" -f ($bytes / 1KB)) }
    return ("{0,6:N0} B " -f $bytes)
}

function Get-TrueCasePath([string]$path) {
    # Steam's registry value is lowercase, so a path built from it reads
    # "c:\program files (x86)\steam" - which looks like something went wrong.
    # Walk it and take the casing the filesystem actually holds.
    try {
        $full = [System.IO.Path]::GetFullPath($path)
        $root = [System.IO.Path]::GetPathRoot($full)
        $rest = $full.Substring($root.Length).TrimEnd('\')
        if (-not $rest) { return $root.ToUpper() }
        $cur = $root.ToUpper()
        foreach ($part in $rest.Split('\')) {
            $dir = New-Object System.IO.DirectoryInfo($cur)
            $hit = @($dir.GetFileSystemInfos($part)) | Select-Object -First 1
            if ($hit) { $cur = PathJoin $cur $hit.Name }
            else { $cur = PathJoin $cur $part }
        }
        return $cur
    } catch { return $path }
}

function Say([string]$text) { Write-Host $text }

function Fail([string]$text) {
    Write-Host ""
    Write-Host "  $text" -ForegroundColor Red
    exit 1
}

function Get-SteamLibraries {
    # Ask Steam where its libraries are rather than guessing drive letters. A
    # hardcoded list only ever finds the machine it was written on.
    $roots = New-Object System.Collections.ArrayList
    foreach ($k in @('HKCU:\Software\Valve\Steam',
                     'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam',
                     'HKLM:\SOFTWARE\Valve\Steam')) {
        $v = $null
        try { $v = Get-ItemProperty $k -ErrorAction SilentlyContinue } catch { continue }
        if (-not $v) { continue }
        foreach ($p in @($v.SteamPath, $v.InstallPath)) {
            # SteamPath comes back lowercase with forward slashes.
            if ($p) { [void]$roots.Add(($p -replace '/', '\')) }
        }
    }

    $libs = New-Object System.Collections.ArrayList
    foreach ($root in $roots) {
        if (-not (Test-Path -PathType Container $root)) { continue }
        if ($libs -notcontains $root) { [void]$libs.Add($root) }
        # Every library folder, including ones on other drives, is listed here.
        $vdf = PathJoin $root 'steamapps\libraryfolders.vdf'
        if (-not (Test-Path -PathType Leaf $vdf)) { continue }
        foreach ($line in [System.IO.File]::ReadAllLines($vdf)) {
            $m = [regex]::Match($line, '"path"\s+"(.+?)"')
            if (-not $m.Success) { continue }
            $lib = $m.Groups[1].Value -replace '\\\\', '\'
            if ($libs -notcontains $lib) { [void]$libs.Add($lib) }
        }
    }
    return $libs
}

function Resolve-BaseDir([string]$path) {
    # Accept the install folder, the base folder inside it, or any folder that
    # merely CONTAINS one of those.
    #
    # That last case is not a nicety. Extract any of the archives Prey is
    # passed around in and you get <extracted>\Prey\base\*.pk4, so dropping
    # the folder you just extracted into - the obvious thing to do - put the
    # paks one level below where this used to look, and Setup said "no .pk4
    # files" at somebody holding a perfectly good copy of the game.
    #
    # Breadth first, so the shallowest install wins, and bounded on both depth
    # and directories visited because somebody will drop a drive root on it.
    if (-not $path) { return $null }
    try {
        if (-not (Test-Path -PathType Container $path)) { return $null }
    } catch { return $null }

    $queue = New-Object System.Collections.Queue
    $queue.Enqueue(@{ Path = $path; Depth = 0 })
    $visited = 0

    while ($queue.Count -gt 0) {
        $node = $queue.Dequeue()
        $visited++
        if ($visited -gt 400) { break }

        foreach ($try in @((PathJoin $node.Path 'base'), $node.Path)) {
            if (Test-Path -PathType Leaf (PathJoin $try 'pak000.pk4')) {
                return (Get-TrueCasePath (Get-Item $try).FullName)
            }
        }
        if ($node.Depth -ge 3) { continue }

        $kids = @()
        try {
            $kids = @(Get-ChildItem -LiteralPath $node.Path -Directory -ErrorAction SilentlyContinue)
        } catch { $kids = @() }
        foreach ($k in $kids) {
            $queue.Enqueue(@{ Path = $k.FullName; Depth = ($node.Depth + 1) })
        }
    }
    return $null
}

function Find-PreyBase {
    # Steam first. The 2017 Arkane game is also called Prey and installs as
    # "Prey"; it has no base\*.pk4, so requiring pak000.pk4 excludes it without
    # having to know anything else about it.
    foreach ($lib in (Get-SteamLibraries)) {
        foreach ($dir in @('Prey 2006', 'Prey')) {
            $b = Resolve-BaseDir (PathJoin $lib "steamapps\common\$dir")
            if ($b) { return $b }
        }
    }

    # Then whatever installed itself and said where it went. This is what finds
    # a retail DVD install, and it also finds Steam's on a machine whose
    # library list is somewhere unusual.
    foreach ($key in @('HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
                       'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall')) {
        $items = @()
        try { $items = Get-ChildItem $key -ErrorAction SilentlyContinue } catch { continue }
        foreach ($it in $items) {
            $d = $null
            try { $d = Get-ItemProperty $it.PSPath -ErrorAction SilentlyContinue } catch { continue }
            if (-not $d -or -not $d.DisplayName) { continue }
            if ($d.DisplayName -notmatch 'Prey') { continue }
            $b = Resolve-BaseDir $d.InstallLocation
            if ($b) { return $b }
        }
    }

    foreach ($guess in @('C:\Program Files (x86)\Prey',
                         'C:\Program Files\Prey',
                         'C:\Program Files (x86)\3D Realms\Prey',
                         'C:\Program Files (x86)\2K Games\Prey',
                         'C:\GOG Games\Prey')) {
        $b = Resolve-BaseDir $guess
        if ($b) { return $b }
    }
    return $null
}

function Split-DroppedLine([string]$line) {
    # What Windows puts on the line when files or a folder are dropped on the
    # console: paths separated by spaces, each one quoted if it contains a
    # space. A path typed by hand may have spaces and no quotes at all, so try
    # the whole line as a single path first - that case is otherwise shredded
    # into pieces that exist nowhere.
    $out = New-Object System.Collections.ArrayList
    $line = $line.Trim()
    if (-not $line) { return $out }

    # Only when it cannot already be seen to be several: two quoted paths in a
    # row is the signature of a multi-file drop, and handing that whole string
    # to Test-Path throws "Illegal characters in path" rather than returning
    # false. The try/catch covers every other way a line can be unusable as a
    # path - a stray quote, a wildcard, a colon in the middle.
    if ($line -notmatch '"\s*"') {
        $whole = $line.Trim('"').Trim()
        $ok = $false
        try { $ok = ($whole -and (Test-Path -LiteralPath $whole)) } catch { $ok = $false }
        if ($ok) {
            [void]$out.Add($whole)
            return $out
        }
    }
    foreach ($m in [regex]::Matches($line, '"([^"]+)"|(\S+)')) {
        $p = $m.Groups[1].Value
        if (-not $p) { $p = $m.Groups[2].Value }
        $p = $p.Trim()
        if ($p) { [void]$out.Add($p) }
    }
    return $out
}

function Add-Sources($dropped, $found) {
    # Turn whatever was dropped - a folder, an install, loose .pk4 files - into
    # source files, keyed by the name they will have in preybase.
    $added = 0
    foreach ($p in $dropped) {
        # A fragment of a mangled line is not merely absent, it can be illegal
        # as a path and throw, which under ErrorActionPreference Stop would end
        # the run instead of skipping one bad entry.
        $exists = $false
        try { $exists = Test-Path -LiteralPath $p } catch { $exists = $false }
        if (-not $exists) {
            Say "    not found: $p"
            continue
        }
        $item = Get-Item -LiteralPath $p
        if ($item.PSIsContainer) {
            $b = Resolve-BaseDir $item.FullName
            $dir = $b
            if (-not $dir) { $dir = $item.FullName }
            $paks = @(Get-ChildItem -LiteralPath $dir -Filter *.pk4 -File -ErrorAction SilentlyContinue)
            if ($paks.Count -eq 0) {
                Say "    no .pk4 files in: $($item.FullName)"

                # If the folder is full of the things people download, say so.
                # "No .pk4 files" is true and unhelpful when you are looking at
                # a downloads folder holding exactly what you need, zipped.
                $arch = @(Get-ChildItem -LiteralPath $item.FullName -File -Recurse -Depth 1 `
                            -Include *.zip, *.7z, *.rar, *.iso, *.bin, *.mdf, *.img `
                            -ErrorAction SilentlyContinue | Select-Object -First 4)
                if ($arch.Count -gt 0) {
                    Say "      It does hold these, which have to be unpacked first:"
                    foreach ($a in $arch) { Say "        $($a.Name)" }
                    Say "      Extract an archive, or mount a disc image and install it,"
                    Say "      then drag the folder with base\pak000.pk4 in it onto here."
                }
                continue
            }
            foreach ($f in $paks) {
                # Never take vr_support.pk4 from a source folder. It is ours,
                # it ships with the release, and pointing this at an older
                # install's preybase would quietly copy that release's copy
                # over this one's.
                if ($f.Name -eq 'vr_support.pk4') { continue }
                $found[$f.Name] = $f.FullName; $added++
            }
            Say "    $($paks.Count) file(s) from $dir"
        }
        elseif ($item.Extension -eq '.pk4') {
            if ($item.Name -eq 'vr_support.pk4') {
                Say "    skipped vr_support.pk4 - the release ships its own"
                continue
            }
            $found[$item.Name] = $item.FullName
            $added++
        }
        else {
            # Say what to do with it. "not a .pk4" is true and useless when
            # somebody has just dropped the thing they downloaded.
            $ext = $item.Extension.ToLower()
            if (@('.zip', '.7z', '.rar') -contains $ext) {
                Say "    $($item.Name) is an archive - extract it somewhere first,"
                Say "      then drag the folder you extracted onto this window."
            }
            elseif (@('.iso', '.bin', '.mdf', '.img', '.cue', '.nrg') -contains $ext) {
                Say "    $($item.Name) is a disc image - right-click it in Explorer and"
                Say "      Mount, run the installer on it, then drag the folder Prey"
                Say "      installed into onto this window."
            }
            elseif ($ext -eq '.exe') {
                Say "    $($item.Name) looks like an installer - run it, then drag the"
                Say "      folder it installed Prey into onto this window."
            }
            else {
                Say "    not a .pk4 or a folder: $($item.Name)"
            }
        }
    }
    return $added
}

function Ask-ForFiles($found) {
    Say ""
    if ($Ask) { Say "  Point this at the Prey data you want to use." }
    else { Say "  Prey was not found automatically." }
    Say ""
    Say "  These are the files needed. They live in the 'base' folder of any"
    Say "  Prey 2006 install, wherever it came from:"
    Say ""
    Say "      Steam        Steam\steamapps\common\Prey 2006\base"
    Say "      Disc         C:\Program Files (x86)\3DRealms\Prey\base"
    Say "                   C:\Program Files (x86)\2K Games\Prey\base"
    Say "      A copy       anywhere you moved or backed the folder up to"
    Say ""
    Say "  Prey 2006 has not been on sale for years, so a disc install or an"
    Say "  old copied folder is the usual case - Steam is not required."
    Say ""
    $i = 0
    $row = "      "
    foreach ($n in $Needed) {
        $row += ("{0,-12}" -f $n)
        $i++
        if ($i % 5 -eq 0) { Say $row; $row = "      " }
    }
    if ($row.Trim()) { Say $row }
    Say ""
    Say "  Drag the 'base' FOLDER onto this window and press Enter."
    Say "  Dragging the files themselves works too - all of them at once, or a"
    Say "  few at a time. Press Enter on an empty line when you are done."
    Say ""

    while ($true) {
        Write-Host "  drop here > " -NoNewline
        $line = Read-Host
        # $null, not "", when stdin ends - a closed console or redirected
        # input reached .Trim() on nothing and threw a PowerShell stack
        # trace at someone who was only trying to install a game.
        if ($null -eq $line -or -not $line.Trim()) { break }
        $n = Add-Sources (Split-DroppedLine $line) $found
        $missing = @($Needed | Where-Object { -not $found.ContainsKey($_) })
        if ($n -gt 0) {
            if ($missing.Count -eq 0) {
                Say "    have all $($found.Count)"
            } elseif ($missing.Count -le 6) {
                Say "    have $($found.Count), still need: $($missing -join ', ')"
            } else {
                $head = ($missing | Select-Object -First 5) -join ', '
                Say "    have $($found.Count), still need $($missing.Count): $head ..."
            }
        }
        if ($missing.Count -eq 0) {
            Say ""
            Say "  All of them. Copying."
            break
        }
    }
}

# ---------------------------------------------------------------------------

$InstallDir = (Get-Item -LiteralPath $InstallDir).FullName
$dest = PathJoin $InstallDir 'preybase'

Say ""
Say "  PreyVR - PCVR : setup"
Say "  ====================="
Say ""

if (-not (Test-Path -PathType Leaf (PathJoin $InstallDir 'PreyVR.exe'))) {
    Fail "Run this from the folder holding PreyVR.exe."
}
# preybase ships empty now, and a zip does not carry an empty directory, so it
# may not exist at all on a fresh extract. It used to be created for us by
# vr_support.pk4 sitting in it.
if (-not (Test-Path -PathType Container $dest)) {
    New-Item -ItemType Directory -Path $dest -Force | Out-Null
}

# vr_support.pk4 is not shipped, and must not be: 31 of its 50 files are
# derived from retail Prey. It is built further down, from this install's own
# paks plus the difference the release carries in tools\pk4patch.
if (-not (Test-Path -PathType Leaf (PathJoin $InstallDir 'tools\pk4patch\manifest.json'))) {
    Fail "tools\pk4patch\manifest.json is missing - this release is incomplete."
}

$found = @{}

$base = $null
if ($PreyDir) {
    $base = Resolve-BaseDir $PreyDir
    if (-not $base) { Fail "No pak000.pk4 under: $PreyDir" }
}
elseif (-not $Ask) {
    $base = Find-PreyBase
}

if ($base) {
    Say "  Found Prey:  $base"
    [void](Add-Sources @($base) $found)
}
else {
    Ask-ForFiles $found
}

if ($found.Count -eq 0) {
    Fail "No game data given, so there is nothing to copy."
}

# What we have against what the port was tested with.
$missing = @($Needed | Where-Object { -not $found.ContainsKey($_) })
$coreMissing = @($Core | Where-Object { -not $found.ContainsKey($_) })
$patchMissing = @($Patch | Where-Object { -not $found.ContainsKey($_) })
if ($coreMissing.Count -gt 0) {
    Say ""
    Say "  Missing the main game data: $($coreMissing -join ', ')"
    Fail "That is not a complete Prey install. Point this at the base folder that has pak000.pk4."
}
if ($patchMissing.Count -gt 0) {
    Say ""
    Say "  Found the game, but not the 1.4 patch: $($patchMissing -join ', ')"
    Say ""
    Say "  Those two files come with Prey's 1.4 update. A disc install needs the"
    Say "  patch applied; Steam and GOG copies already have it. This port cannot"
    Say "  be built without them - some of the files it patches live inside."
    Fail "Apply the Prey 1.4 patch to your install, then run this again."
}

# Copy. A file already there at the same size is left alone, so running this
# twice costs nothing and an interrupted copy can simply be run again.
Say ""
$copied = 0; $skipped = 0; $bytes = 0
$plan = @()
foreach ($name in ($found.Keys | Sort-Object)) {
    $src = Get-Item -LiteralPath $found[$name]
    $dst = PathJoin $dest $name
    if ((Test-Path -LiteralPath $dst) -and ((Get-Item -LiteralPath $dst).Length -eq $src.Length)) {
        $skipped++
        continue
    }
    $plan += ,@($src, $dst)
    $bytes += $src.Length
}

if ($plan.Count -gt 0) {
    $drive = [System.IO.Path]::GetPathRoot($dest)
    $free = (New-Object System.IO.DriveInfo($drive)).AvailableFreeSpace
    if ($free -lt ($bytes + 100MB)) {
        Fail ("Not enough room on {0} - {1} needed, {2} free." -f $drive, (Fmt-Size $bytes).Trim(), (Fmt-Size $free).Trim())
    }
    Say ("  Copying {0} file(s), {1}" -f $plan.Count, (Fmt-Size $bytes).Trim())
    Say ""
    $n = 0
    foreach ($pair in $plan) {
        $n++
        $src = $pair[0]; $dst = $pair[1]
        Write-Host ("    [{0,2}/{1}] {2,-12} {3}  " -f $n, $plan.Count, $src.Name, (Fmt-Size $src.Length)) -NoNewline
        Copy-Item -LiteralPath $src.FullName -Destination $dst -Force
        $copied++
        Write-Host "ok"
    }
}
else {
    Say "  Everything is already in place."
}
if ($skipped -gt 0) { Say "  $skipped file(s) were already there." }

# Report the version, having copied either way. A size that differs from the
# tested build is worth knowing about and is not an error: a different release
# or a different language will differ and will still play.
$odd = @()
foreach ($name in $Tested.Keys) {
    $dst = PathJoin $dest $name
    if (-not (Test-Path -LiteralPath $dst)) { continue }
    $len = (Get-Item -LiteralPath $dst).Length
    if ($len -ne $Tested[$name]) { $odd += "$name is $len, tested with $($Tested[$name])" }
}

# ---------------------------------------------------------------------------
# Build vr_support.pk4, now that the retail paks are here.
#
# The release ships the difference rather than the content - see buildpk4.ps1
# for why and how. This is the step that turns the two halves into a pk4, on
# the machine of somebody who already owns the game.
Say ""
Say "  Building vr_support.pk4 from your own Prey data."
$bp = PathJoin $InstallDir 'tools\buildpk4.ps1'
if (-not (Test-Path -LiteralPath $bp)) { Fail "tools\buildpk4.ps1 is missing." }
& powershell -NoProfile -ExecutionPolicy Bypass -File $bp `
    -PatchDir (PathJoin $InstallDir 'tools\pk4patch') `
    -PreyBase $dest `
    -OutFile (PathJoin $dest 'vr_support.pk4')
if ($LASTEXITCODE -ne 0) {
    Fail "Could not build vr_support.pk4. The game will not run without it."
}

if ($Verify) {
    Say ""
    Say "  Hashing what was copied."
    foreach ($name in ($found.Keys | Sort-Object)) {
        $dst = PathJoin $dest $name
        if (-not (Test-Path -LiteralPath $dst)) { continue }
        $h = (Get-FileHash -LiteralPath $dst -Algorithm SHA256).Hash
        Say ("    {0,-12} {1}" -f $name, $h.ToLower())
    }
}

Say ""
Say "  ---------------------------------------------------------------"
$have = @(Get-ChildItem -LiteralPath $dest -Filter *.pk4 -File).Count
Say ("  preybase now holds {0} .pk4 files." -f $have)

if ($missing.Count -gt 0) {
    Say ""
    Say "  Not found, and the port was tested with them:"
    Say ("      {0}" -f ($missing -join ', '))
    Say "  The game will probably still run. If something is missing in it,"
    Say "  this is the first thing to look at."
}
if ($odd.Count -gt 0) {
    Say ""
    Say "  Different from the version this was tested against:"
    foreach ($o in $odd) { Say "      $o" }
    Say "  Most likely a different release or language. Not a problem by itself."
}

Say ""
Say "  Ready. Start Virtual Desktop, put the headset on, and run:"
Say ""
Say "      Play PreyVR.bat"
Say ""
Say "  No headset? Play PreyVR flatscreen.bat runs it in a window."
Say ""
exit 0
