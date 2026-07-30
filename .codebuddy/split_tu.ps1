# Internal one-off helper: move member-function definitions from a large
# translation unit into a new TU. Not part of the build system.
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Target,
    [Parameter(Mandatory = $true)][string]$Banner,
    [Parameter(Mandatory = $true)][string]$IncludeLine,
    [Parameter(Mandatory = $true)][string[]]$Funcs,
    [switch]$ListOnly
)

$ErrorActionPreference = 'Stop'
$lines = [System.IO.File]::ReadAllLines($Source)

# 1) Detect top-level member function definition start lines.
$rx = [regex]'([A-Za-z_]\w*::~?[A-Za-z_]\w*)\s*\('
$defs = New-Object System.Collections.Generic.List[object]
for ($i = 0; $i -lt $lines.Count; $i++) {
    $l = $lines[$i]
    if ($l.Length -eq 0 -or [char]::IsWhiteSpace($l[0])) { continue }
    if ($l.StartsWith('//') -or $l.StartsWith('#') -or $l.StartsWith('}') -or $l.StartsWith('{')) { continue }
    if ($l -match ';\s*$') { continue }
    $ms = $rx.Matches($l)
    if ($ms.Count -gt 0) {
        $defs.Add([pscustomobject]@{ Line = $i; Name = $ms[$ms.Count - 1].Groups[1].Value })
    }
}

if ($ListOnly) {
    $defs | ForEach-Object { "{0}: {1}" -f ($_.Line + 1), $_.Name }
    exit 0
}

# 2) Compute adjusted starts (include leading comment block + one blank line).
function AdjStart([int]$defLine) {
    $s = $defLine
    while ($s -gt 0 -and $lines[$s - 1] -match '^\s*//') { $s-- }
    if ($s -gt 0 -and $lines[$s - 1].Trim() -eq '') { $s-- }
    return $s
}

$starts = @{}
for ($k = 0; $k -lt $defs.Count; $k++) { $starts[$k] = AdjStart $defs[$k].Line }

# 3) Build extents for requested functions.
$nameToIdx = @{}
for ($k = 0; $k -lt $defs.Count; $k++) {
    if ($nameToIdx.ContainsKey($defs[$k].Name)) { throw "Duplicate definition name: $($defs[$k].Name)" }
    $nameToIdx[$defs[$k].Name] = $k
}

$remove = New-Object bool[] $lines.Count
$movedIdx = New-Object System.Collections.Generic.List[int]
foreach ($f in $Funcs) {
    if (-not $nameToIdx.ContainsKey($f)) {
        $avail = ($defs | ForEach-Object { $_.Name }) -join "`n"
        throw "Function not found: $f`nAvailable:`n$avail"
    }
    $k = $nameToIdx[$f]
    $a = $starts[$k]
    $b = if ($k + 1 -lt $defs.Count) { $starts[$k + 1] - 1 } else { $lines.Count - 1 }
    for ($i = $a; $i -le $b; $i++) { $remove[$i] = $true }
    $movedIdx.Add($k)
}

# 4) Emit target file (functions in original file order).
$out = New-Object System.Collections.Generic.List[string]
foreach ($b in ($Banner -split "\r?\n")) { $out.Add($b) }
$out.Add($IncludeLine)
$prevBlank = $false
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($remove[$i]) { $out.Add($lines[$i]) }
}

# 5) Rewrite source with remaining lines.
$rest = New-Object System.Collections.Generic.List[string]
for ($i = 0; $i -lt $lines.Count; $i++) {
    if (-not $remove[$i]) { $rest.Add($lines[$i]) }
}

$enc = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($Target, (($out -join "`n") + "`n"), $enc)
[System.IO.File]::WriteAllText($Source, (($rest -join "`n") + "`n"), $enc)

Write-Output ("Moved {0} functions, {1} lines. {2} -> {3} lines remaining." -f $Funcs.Count, ($remove | Where-Object { $_ }).Count, $lines.Count, $rest.Count)
