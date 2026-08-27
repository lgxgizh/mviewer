param(
    [string]$RepoRoot = (Split-Path $PSScriptRoot -Parent)
)

$ErrorActionPreference = 'Stop'
. (Join-Path $RepoRoot 'scripts/release_version.ps1')
$failures = 0

function Check([bool]$Condition, [string]$Message) {
    if ($Condition) {
        Write-Host "PASS: $Message"
    } else {
        Write-Host "FAIL: $Message"
        $script:failures++
    }
}

$three = Get-ReleaseVersionIdentity '1.0.13'
Check ($three.Version -eq '1.0.13' -and $three.WindowsVersion -eq '1.0.13.0') `
    'three-component versions normalize to four Windows components'
$four = Get-ReleaseVersionIdentity '1.0.13.7'
Check ($four.Version -eq '1.0.13.7' -and $four.WindowsVersion -eq '1.0.13.7') `
    'four-component versions are preserved'
$tagged = Get-ReleaseVersionIdentity 'v2.4.6'
Check ($tagged.Version -eq '2.4.6' -and $tagged.WindowsVersion -eq '2.4.6.0') `
    'tag-prefixed versions normalize consistently'

foreach ($invalid in @('', '1.2', '1.2.3-beta', '1.2.3.4.5', '70000.0.0')) {
    try {
        Get-ReleaseVersionIdentity $invalid | Out-Null
        Check $false "invalid version '$invalid' is rejected"
    } catch {
        Check $true "invalid version '$invalid' is rejected"
    }
}

Write-Host "release_version_tests: $(if ($failures -eq 0) { 'PASS' } else { 'FAIL' })"
exit $(if ($failures -eq 0) { 0 } else { 1 })
