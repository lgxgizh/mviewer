<##
.SYNOPSIS
    Shared release-version identity validation for the package and installer.

.DESCRIPTION
    A release version must be numeric X.Y.Z or X.Y.Z.W. The display/package
    version is preserved, while Windows PE metadata always receives exactly
    four numeric components. No caller should rely on an installer fallback.
#>
param(
    [string]$ReleaseVersion = "",
    [switch]$JsonOutput
)

$ErrorActionPreference = 'Stop'

function Get-ReleaseVersionIdentity {
    param([Parameter(Mandatory = $true)][string]$InputVersion)

    $value = $InputVersion.Trim()
    if ($value.StartsWith('v')) { $value = $value.Substring(1) }
    if ($value -notmatch '^(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?$') {
        throw "Release version must be X.Y.Z or X.Y.Z.W: '$InputVersion'"
    }

    $parts = @([int64]$Matches[1], [int64]$Matches[2], [int64]$Matches[3])
    if ($Matches[4] -ne $null) { $parts += [int64]$Matches[4] }
    foreach ($part in $parts) {
        if ($part -lt 0 -or $part -gt 65535) {
            throw "Release version component is outside the Windows metadata range 0..65535: '$InputVersion'"
        }
    }

    $displayVersion = ($parts -join '.')
    $windowsParts = @($parts)
    if ($windowsParts.Count -eq 3) { $windowsParts += 0 }
    [pscustomobject]@{
        Version = $displayVersion
        WindowsVersion = ($windowsParts -join '.')
    }
}

if ($ReleaseVersion -ne '') {
    $identity = Get-ReleaseVersionIdentity -InputVersion $ReleaseVersion
    if ($JsonOutput) {
        $identity | ConvertTo-Json -Compress
    } else {
        Write-Output "Version=$($identity.Version)"
        Write-Output "WindowsVersion=$($identity.WindowsVersion)"
    }
}
