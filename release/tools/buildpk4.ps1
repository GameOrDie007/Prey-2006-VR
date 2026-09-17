# Build vr_support.pk4 from the player's own Prey install.
#
# The release cannot carry that pk4. 31 of its 50 files are derived from retail
# Prey - Human Head's menu, HUD, weapon models and map scripts, modified but
# still theirs - and redistributing them is not ours to do. So the release
# carries the difference instead, and this puts the two halves together on the
# machine of somebody who already owns the game:
#
#   tools\pk4patch\new\          19 files with no retail counterpart. Copied.
#   tools\pk4patch\manifest.json 23 patches, each a list of copy/skip/add
#                                operations over the lines of the retail
#                                original, plus that original's SHA256.
#
# Deliberately not a unified diff: this has to apply with nothing installed but
# Windows PowerShell.
#
# A Prey install whose file does not match the SHA256 we diffed against is
# refused rather than patched. Building a subtly wrong menu or a subtly wrong
# weapon model, with nobody able to say why, is the outcome worth avoiding.

param(
    [Parameter(Mandatory = $true)][string]$PatchDir,   # tools\pk4patch
    [Parameter(Mandatory = $true)][string]$PreyBase,   # preybase, holding pak000.pk4 ...
    [Parameter(Mandatory = $true)][string]$OutFile     # preybase\vr_support.pk4
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Fail([string]$m) { Write-Host ""; Write-Host "  $m"; exit 1 }

# Absolute from here on. The new-files loop below trims $newDir.Length off
# each file's FullName, which is always absolute - so a RELATIVE -PatchDir
# trims too few characters and every file lands in the pk4 under a mangled
# path like tools/release/pk4patch/new/guis/... Setup always passes an
# absolute path, so this only appeared when the tool was run by hand.
if (Test-Path -LiteralPath $PatchDir) {
    # .ProviderPath, not .Path: on a UNC share .Path comes back prefixed
    # with Microsoft.PowerShell.Core\FileSystem:: , which is LONGER than the
    # real path, and the Substring below then throws instead of trimming.
    $PatchDir = (Resolve-Path -LiteralPath $PatchDir).ProviderPath
}

$manifestPath = Join-Path $PatchDir 'manifest.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { Fail "No manifest at $manifestPath" }
$man = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json

# ---------------------------------------------------------------- the paks
#
# Retail paks only, and the retail load order among them.
#
# This used to index every .pk4 in the folder and let the later name win, on the
# reasoning that it is the engine's own load order. That is true for the engine
# and wrong for us. A modded install - and the machine this was written on has
# eleven mods in that folder - has things like revelations_demo.pk4 shipping its
# own guis/mainmenu/mainmenu_newgame.guifragment, and "r" sorts after "p", so
# the entry we patched against was the mod's file rather than Human Head's. The
# SHA256 check caught it and refused to build, which is the right failure and a
# baffling one to read: it said "a different release or language" about a
# perfectly ordinary English install.
#
# The patches were made against retail, so retail is what they apply to. Mods
# are ignored here. What the engine then loads on top of vr_support.pk4 at run
# time is the player's business.
$RetailPaks = @('pak000.pk4', 'pak001.pk4', 'pak002.pk4', 'pak003.pk4',
                'pak004.pk4', 'pak005.pk4', 'pak006.pk4', 'pak020.pk4',
                'pak040.pk4')
$zips = @()
$index = @{}
$from = @{}
foreach ($name in $RetailPaks) {
    $full = Join-Path $PreyBase $name
    if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { continue }
    try { $z = [System.IO.Compression.ZipFile]::OpenRead($full) }
    catch { continue }
    $zips += $z
    foreach ($e in $z.Entries) {
        if ($e.FullName.EndsWith('/')) { continue }
        $key = $e.FullName.ToLower().Replace('\', '/')
        $index[$key] = $e
        $from[$key] = $name
    }
}
if ($index.Count -eq 0) {
    Fail "No readable retail .pk4 files in $PreyBase (pak000.pk4 and friends)"
}

function Read-Entry($entry) {
    $s = $entry.Open()
    $ms = New-Object System.IO.MemoryStream
    $s.CopyTo($ms)
    $s.Close()
    return $ms.ToArray()
}

# latin-1 throughout: these are 8-bit text files and every byte must survive the
# round trip, which UTF-8 would not guarantee.
$latin1 = [System.Text.Encoding]::GetEncoding(28591)

$tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("vrsupport_" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp -Force | Out-Null

try {
    # ------------------------------------------------------------ patched
    $patched = 0
    foreach ($rel in $man.patch.PSObject.Properties.Name) {
        $info = $man.patch.$rel
        $key = $rel.ToLower()
        if (-not $index.ContainsKey($key)) {
            Fail "Your Prey install has no $rel - setup cannot build vr_support.pk4."
        }
        $bytes = Read-Entry $index[$key]

        $sha = [System.Security.Cryptography.SHA256]::Create()
        $have = ([BitConverter]::ToString($sha.ComputeHash($bytes)) -replace '-', '').ToLower()
        if ($have -ne $info.sha256) {
            Fail ("$rel in your Prey install is not the version this was built " +
                  "against.`n  It came from $($from[$key])`n" +
                  "  Expected $($info.sha256)`n  Found    $have`n" +
                  "  A different release or language, or a modified pak. Please " +
                  "report it - the port can be rebuilt for it.")
        }

        $src = $latin1.GetString($bytes) -split "`n"
        $out = New-Object System.Collections.Generic.List[string]
        $i = 0
        foreach ($op in $info.ops) {
            switch ($op[0]) {
                'copy' { for ($k = 0; $k -lt $op[1]; $k++) { $out.Add($src[$i]); $i++ } }
                'skip' { $i += $op[1] }
                'add'  { foreach ($line in $op[1]) { $out.Add($line) } }
                default { Fail "Unknown patch operation '$($op[0])' in $rel" }
            }
        }

        $dest = Join-Path $tmp ($rel -replace '/', '\')
        New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
        [System.IO.File]::WriteAllBytes($dest, $latin1.GetBytes(($out -join "`n")))
        $patched++
    }

    # ---------------------------------------------------------------- new
    $newDir = Join-Path $PatchDir 'new'
    $copied = 0
    if (Test-Path -LiteralPath $newDir) {
        foreach ($f in (Get-ChildItem -LiteralPath $newDir -Recurse -File)) {
            $rel = $f.FullName.Substring($newDir.Length).TrimStart('\')
            $dest = Join-Path $tmp $rel
            New-Item -ItemType Directory -Path (Split-Path -Parent $dest) -Force | Out-Null
            Copy-Item -LiteralPath $f.FullName -Destination $dest -Force
            $copied++
        }
    }

    # --------------------------------------------------------------- zip
    if (Test-Path -LiteralPath $OutFile) { Remove-Item -LiteralPath $OutFile -Force }
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $tmp, $OutFile, [System.IO.Compression.CompressionLevel]::Optimal, $false)

    Write-Host ("    built vr_support.pk4 - {0} patched, {1} new" -f $patched, $copied)
}
finally {
    foreach ($z in $zips) { $z.Dispose() }
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

exit 0
