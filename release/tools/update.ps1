#requires -version 5
<#
    Carry a previous PreyVR PCVR install into this one.

    Setup's job is to find Prey and build vr_support.pk4 out of it. None of that
    changes between builds, so doing it again for every new version is a waste
    of a gigabyte and several minutes - and it is why a tester ends up with
    twenty folders and no patience.

    This takes the parts that are yours from an older install and puts them
    here:

        the retail pak files      hardlinked where the filesystem allows it,
                                  so they cost nothing twice
        vr_support.pk4            reused when the patch set that built it has
                                  not changed, rebuilt from your own paks when
                                  it has - stale is worse than missing, because
                                  the game starts and your change is silently
                                  absent
        your savegames            copied
        your settings             merged: every setting and key bind you have
                                  keeps its value, and anything new in this
                                  version is added. A new bind must not be lost
                                  to an old config, and an old preference must
                                  not be lost to a new one.

    Nothing is written to the old install. It stays exactly as it is, so if
    this version is worse you still have the one that worked.

        Update.bat                            use the newest install it finds
        Update.bat -From "D:\PreyVR-PCVR-1.0.7"
#>
param(
    [string] $Root = '.',
    [string] $From = ''
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path $Root).ProviderPath

function Say([string] $m) { Write-Host $m }
function Fail([string] $m) { Write-Host ''; Write-Host "  $m"; exit 1 }

Say ''
Say '  PreyVR PCVR - carrying over your previous install'
Say ''

# ---------------------------------------------------------------- find it
function Looks-Like-Install([string] $p) {
    return (Test-Path (Join-Path $p 'preybase\pak000.pk4')) -and
           (Test-Path (Join-Path $p 'PreyVR.exe'))
}

if ($From) {
    $From = $From.TrimEnd('\')
    if (-not (Test-Path $From)) { Fail "There is nothing at $From" }
    $From = (Resolve-Path $From).ProviderPath
    if (-not (Looks-Like-Install $From)) {
        Fail "$From does not look like a PreyVR install - no preybase\pak000.pk4"
    }
} else {
    # Newest sibling folder that has game data and is not this one.
    $parent = Split-Path $Root -Parent
    $cand = @(Get-ChildItem $parent -Directory -ErrorAction SilentlyContinue |
              Where-Object { $_.FullName -ne $Root -and (Looks-Like-Install $_.FullName) } |
              Sort-Object LastWriteTime -Descending)
    if ($cand.Count -eq 0) {
        Say '  No previous install found next to this one.'
        Say ''
        Say '  Run Setup.bat instead, or name the old folder:'
        Say ''
        Say '      Update.bat -From "D:\path\to\PreyVR-PCVR-1.0.7"'
        Say ''
        exit 1
    }
    $From = $cand[0].FullName
}

if ($From -eq $Root) { Fail 'That is this folder.' }
Say "  from   $From"
Say "  to     $Root"
Say ''

# ------------------------------------------------------------- the paks
$srcBase = Join-Path $From 'preybase'
$dstBase = Join-Path $Root 'preybase'
if (-not (Test-Path $dstBase)) { New-Item -ItemType Directory $dstBase | Out-Null }

$linked = 0; $copied = 0
foreach ($f in (Get-ChildItem $srcBase -Filter *.pk4 -File)) {
    if ($f.Name -ieq 'vr_support.pk4') { continue }   # handled below
    $dst = Join-Path $dstBase $f.Name
    if (Test-Path $dst) { continue }
    # A hardlink costs no space and cannot drift from the original. It only
    # works within one volume, so fall back rather than insisting.
    $ok = $false
    try {
        New-Item -ItemType HardLink -Path $dst -Target $f.FullName -ErrorAction Stop | Out-Null
        $ok = $true; $linked++
    } catch { $ok = $false }
    if (-not $ok) { Copy-Item $f.FullName $dst; $copied++ }
}
Say ("  game data: {0} linked, {1} copied" -f $linked, $copied)

# ------------------------------------------------------- vr_support.pk4
#
# Reused only when the patch set it was built from is byte-identical to the one
# shipped here. A carried pk4 from an older patch set is the exact failure this
# project has already shipped twice: the game runs, and the change you are
# testing is simply not in it.
function Hash-Of([string] $p) {
    if (-not (Test-Path $p)) { return '' }
    return (Get-FileHash $p -Algorithm SHA256).Hash
}
$oldMan = Hash-Of (Join-Path $From 'tools\pk4patch\manifest.json')
$newMan = Hash-Of (Join-Path $Root 'tools\pk4patch\manifest.json')
$srcPk4 = Join-Path $srcBase 'vr_support.pk4'
$dstPk4 = Join-Path $dstBase 'vr_support.pk4'

if ($oldMan -and $newMan -and $oldMan -eq $newMan -and (Test-Path $srcPk4)) {
    Copy-Item $srcPk4 $dstPk4 -Force
    Say '  vr_support.pk4: carried over, the patch set is unchanged'
} else {
    Say '  vr_support.pk4: the patch set changed, rebuilding it from your paks'
    $bp = Join-Path $Root 'tools\buildpk4.ps1'
    if (-not (Test-Path $bp)) { Fail "tools\buildpk4.ps1 is missing from this build" }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $bp `
        -PatchDir (Join-Path $Root 'tools\pk4patch') `
        -PreyBase $dstBase -OutFile $dstPk4
    if ($LASTEXITCODE -ne 0) { Fail 'Rebuilding vr_support.pk4 failed - the message above says why.' }
}

# --------------------------------------------------------------- savegames
$srcSave = Join-Path $From 'saves\preybase\savegames'
$dstSave = Join-Path $Root 'saves\preybase\savegames'
$n = 0
if (Test-Path $srcSave) {
    if (-not (Test-Path $dstSave)) { New-Item -ItemType Directory $dstSave -Force | Out-Null }
    foreach ($f in (Get-ChildItem $srcSave -File)) {
        Copy-Item $f.FullName (Join-Path $dstSave $f.Name) -Force
        $n++
    }
}
Say ("  savegames: {0} files" -f $n)

# ---------------------------------------------------------------- settings
#
# Merge, do not choose. Keeping the old file loses binds and defaults added
# since; keeping the new one loses everything the player has set. So: the old
# file wins on every key it already has, and any key only the new one has is
# appended.
function Key-Of([string] $line) {
    $t = $line.Trim()
    if ($t -match '^(seta|set|bind)\s+"?([^"\s]+)"?') {
        return ($matches[1].ToLower() + ' ' + $matches[2].ToLower())
    }
    return ''
}

$oldCfg = Join-Path $From 'saves\preybase\preyconfig.cfg'
$newCfg = Join-Path $Root 'saves\preybase\preyconfig.cfg'
if ((Test-Path $oldCfg) -and (Test-Path $newCfg)) {
    $oldLines = @(Get-Content $oldCfg)
    $have = @{}
    foreach ($l in $oldLines) { $k = Key-Of $l; if ($k) { $have[$k] = $true } }

    $added = @()
    foreach ($l in (Get-Content $newCfg)) {
        $k = Key-Of $l
        if ($k -and -not $have.ContainsKey($k)) { $added += $l }
    }

    $out = @($oldLines)
    if ($added.Count -gt 0) {
        $out += ''
        $out += '// Added by Update.bat - new in this version, not in your old config.'
        $out += $added
    }
    Set-Content -Path $newCfg -Value $out -Encoding ASCII
    Say ("  settings: your config kept, {0} new line(s) added" -f $added.Count)
} else {
    Say '  settings: no previous config, using the shipped one'
}

# autoexec.cfg is the player's file and ships entirely commented out. If theirs
# differs from the shipped one they have edited it, so it comes with them.
$oldAuto = Join-Path $From 'saves\preybase\autoexec.cfg'
$newAuto = Join-Path $Root 'saves\preybase\autoexec.cfg'
if ((Test-Path $oldAuto) -and (Test-Path $newAuto)) {
    if ((Hash-Of $oldAuto) -ne (Hash-Of $newAuto)) {
        Copy-Item $newAuto "$newAuto.shipped" -Force
        Copy-Item $oldAuto $newAuto -Force
        Say '  autoexec.cfg: yours was edited, carried over (the new one is autoexec.cfg.shipped)'
    }
}

Say ''
Say '  Done. Play PreyVR.bat is ready - nothing in the old folder was changed.'
Say ''
exit 0
