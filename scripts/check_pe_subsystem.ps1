<##
.SYNOPSIS
    Fail unless every given Windows PE file has the expected subsystem.

.DESCRIPTION
    Reads the PE optional header directly (no dumpbin needed), so it runs on
    any runner with PowerShell. MViewer.exe must be IMAGE_SUBSYSTEM_WINDOWS_GUI
    (2): no console window when launched from Explorer. A console build is
    IMAGE_SUBSYSTEM_WINDOWS_CUI (3).

.EXAMPLE
    ./scripts/check_pe_subsystem.ps1 -Path build_msvc/src/MViewer.exe -Expected 2
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string[]]$Path,
    [int]$Expected = 2
)

$ErrorActionPreference = 'Stop'

function Get-PeSubsystem([string]$File) {
    $bytes = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $File).Path)
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "'$File' is not a PE file (no MZ header)"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 24 + 70 -gt $bytes.Length -or
        $bytes[$peOffset] -ne 0x50 -or $bytes[$peOffset + 1] -ne 0x45 -or
        $bytes[$peOffset + 2] -ne 0 -or $bytes[$peOffset + 3] -ne 0) {
        throw "'$File' has no PE signature"
    }
    # Optional header follows the 4-byte signature and the 20-byte COFF header.
    # Subsystem sits at offset 68 for both PE32 (0x10B) and PE32+ (0x20B).
    $optional = $peOffset + 24
    $magic = [BitConverter]::ToUInt16($bytes, $optional)
    if ($magic -ne 0x10B -and $magic -ne 0x20B) {
        throw "'$File' has an unknown optional-header magic 0x$($magic.ToString('X'))"
    }
    return [int][BitConverter]::ToUInt16($bytes, $optional + 68)
}

$names = @{ 1 = 'NATIVE'; 2 = 'WINDOWS_GUI'; 3 = 'WINDOWS_CUI' }
$failed = $false
foreach ($file in $Path) {
    $subsystem = Get-PeSubsystem $file
    $label = if ($names.ContainsKey($subsystem)) { $names[$subsystem] } else { 'other' }
    if ($subsystem -eq $Expected) {
        Write-Host "PE subsystem OK: $file = $subsystem ($label)"
    } else {
        Write-Host "PE subsystem FAIL: $file = $subsystem ($label), expected $Expected"
        $failed = $true
    }
}
if ($failed) { exit 1 }
