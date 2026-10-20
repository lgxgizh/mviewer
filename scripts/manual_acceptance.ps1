# Manual-acceptance wizard for native Windows MANUAL/BLOCKED qualification.
#
# ASCII-only source so Windows PowerShell 5.1 can parse it under a non-UTF-8
# code page (same rule as scripts/architecture_gate.ps1).
#
# Usage:
#   pwsh -File scripts/manual_acceptance.ps1 [-Exe <MViewer.exe>] [-List]
#   pwsh -File scripts/manual_acceptance.ps1 -DryRun -NonInteractive
#
# Exit: 0 = every executed item passed; 1 = at least one FAIL; 2 = usage/validation.

[CmdletBinding()]
param(
    [string]$RepoRoot = '',
    [string]$Exe = '',
    [string]$Checklist = '',
    [string[]]$Only = @(),
    [string]$Resume = '',
    [string]$OutDir = '',
    [string]$SampleDir = '',
    [switch]$List,
    [switch]$NonInteractive,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$script:Quiet = [bool]($NonInteractive -or $DryRun)

function Get-DefaultRepoRoot {
    return (Split-Path -Parent $PSScriptRoot)
}

function Write-Utf8NoBom {
    param([string]$Path, [string]$Value)
    $dir = Split-Path -Parent $Path
    if ($dir -and -not (Test-Path -LiteralPath $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $encoding = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText($Path, $Value, $encoding)
}

function Read-Utf8Text {
    param([string]$Path)
    $encoding = New-Object System.Text.UTF8Encoding $false, $true
    return $encoding.GetString([System.IO.File]::ReadAllBytes($Path))
}

function Test-IsWindowsOs {
    return ($env:OS -eq 'Windows_NT')
}

function ConvertTo-StringArray {
    param($Value)
    if ($null -eq $Value) { return @() }
    return @($Value | ForEach-Object { [string]$_ })
}

function Get-PropertyValue {
    param($Object, [string]$Name)
    if ($null -eq $Object) { return $null }
    $prop = $Object.PSObject.Properties[$Name]
    if ($null -eq $prop) { return $null }
    return $prop.Value
}

function Resolve-RepoPath {
    param([string]$Root, [string]$Relative)
    $normalized = $Relative -replace '/', [IO.Path]::DirectorySeparatorChar
    return (Join-Path $Root $normalized)
}

function Parse-OnlyIds {
    param([string[]]$Raw)
    $ids = New-Object System.Collections.Generic.List[string]
    foreach ($part in @($Raw)) {
        if ([string]::IsNullOrWhiteSpace($part)) { continue }
        foreach ($bit in $part.Split(',')) {
            $trim = $bit.Trim()
            if ($trim) { [void]$ids.Add($trim) }
        }
    }
    return @($ids)
}

function Read-ChecklistDocument {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "checklist not found: $Path"
    }
    $text = Read-Utf8Text $Path
    try {
        return ($text | ConvertFrom-Json)
    }
    catch {
        throw "checklist is not valid JSON: $($_.Exception.Message)"
    }
}

function Test-ChecklistDocument {
    param($Document, [string]$Repo)
    $errors = New-Object System.Collections.Generic.List[string]
    if ($null -eq $Document) {
        $errors.Add('checklist JSON is empty')
        return @($errors)
    }
    $items = @(Get-PropertyValue $Document 'items')
    if ($items.Count -eq 0) { $errors.Add('checklist has no items[]') }
    $seen = @{}
    $index = 0
    foreach ($item in $items) {
        $index++
        $id = [string](Get-PropertyValue $item 'id')
        if ([string]::IsNullOrWhiteSpace($id)) {
            $errors.Add("items[$index] is missing id")
            continue
        }
        if ($seen.ContainsKey($id)) { $errors.Add("duplicate id '$id'") }
        else { $seen[$id] = $true }
        if ([string]::IsNullOrWhiteSpace([string](Get-PropertyValue $item 'title'))) {
            $errors.Add("$id is missing title")
        }
        $expected = [string](Get-PropertyValue $item 'expected')
        if ([string]::IsNullOrWhiteSpace($expected)) { $errors.Add("$id is missing expected") }
        $steps = @(Get-PropertyValue $item 'steps')
        if ($steps.Count -eq 0) { $errors.Add("$id has no steps") }
        $prereq = Get-PropertyValue $item 'prerequisites'
        if ($null -eq $prereq) { $errors.Add("$id is missing prerequisites") }
        $source = Get-PropertyValue $item 'source'
        $report = [string](Get-PropertyValue $source 'report')
        $row = Get-PropertyValue $source 'row'
        if ([string]::IsNullOrWhiteSpace($report)) { $errors.Add("$id is missing source.report") }
        if ($null -eq $row -or [string]::IsNullOrWhiteSpace([string]$row)) {
            $errors.Add("$id is missing source.row")
        }
        if (-not [string]::IsNullOrWhiteSpace($report)) {
            $reportPath = Resolve-RepoPath $Repo $report
            if (-not (Test-Path -LiteralPath $reportPath)) {
                $errors.Add("$id source report missing: $report")
            }
        }
        foreach ($extra in @(Get-PropertyValue $item 'also')) {
            $extraReport = [string](Get-PropertyValue $extra 'report')
            if ([string]::IsNullOrWhiteSpace($extraReport)) { continue }
            $extraPath = Resolve-RepoPath $Repo $extraReport
            if (-not (Test-Path -LiteralPath $extraPath)) {
                $errors.Add("$id also-report missing: $extraReport")
            }
        }
    }
    return @($errors)
}

function Join-Parts {
    param([string]$Root, [string[]]$Parts)
    $path = $Root
    foreach ($part in @($Parts)) { $path = Join-Path $path $part }
    return $path
}

function Find-MViewerExe {
    param([string]$Root, [string]$Explicit)
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        if (Test-Path -LiteralPath $Explicit) {
            return ((Resolve-Path -LiteralPath $Explicit).Path)
        }
        throw "MViewer.exe not found: $Explicit"
    }
    $candidates = @(
        (Join-Parts $Root @('build_msvc', 'bin', 'MViewer.exe')),
        (Join-Parts $Root @('build_msvc', 'bin', 'Release', 'MViewer.exe')),
        (Join-Parts $Root @('bin', 'MViewer.exe')),
        (Join-Parts $Root @('bin', 'Release', 'MViewer.exe'))
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return ((Resolve-Path -LiteralPath $candidate).Path)
        }
    }
    foreach ($searchRoot in @((Join-Path $Root 'dist'), (Join-Path $Root 'build_msvc'))) {
        if (-not (Test-Path -LiteralPath $searchRoot)) { continue }
        $hit = @(Get-ChildItem -LiteralPath $searchRoot -Recurse -Filter 'MViewer.exe' -ErrorAction SilentlyContinue |
            Select-Object -First 1)
        if ($hit.Count -gt 0) { return $hit[0].FullName }
    }
    return $null
}

function Resolve-ChecklistFile {
    param([string]$Root, [string]$Requested)
    $defaultPath = Join-Parts $Root @('docs', 'acceptance', 'manual_acceptance_checklist.json')
    if ([string]::IsNullOrWhiteSpace($Requested)) { return $defaultPath }
    if ([IO.Path]::IsPathRooted($Requested)) { return $Requested }
    $fromCwd = Join-Path (Get-Location).Path $Requested
    if (Test-Path -LiteralPath $fromCwd) { return $fromCwd }
    return (Join-Path $Root $Requested)
}

function Resolve-OutputDirectory {
    param([string]$Root, [string]$Requested, [string]$ResumePath)
    if (-not [string]::IsNullOrWhiteSpace($Requested)) {
        if ([IO.Path]::IsPathRooted($Requested)) { return $Requested }
        return (Join-Path (Get-Location).Path $Requested)
    }
    if ($ResumePath) {
        return (Split-Path -Parent (Resolve-Path -LiteralPath $ResumePath).Path)
    }
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    return (Join-Parts $Root @('artifacts', 'manual_acceptance', $stamp))
}

function Get-ExeVersionInfo {
    param([string]$Path)
    $info = [ordered]@{ path = $Path; fileVersion = $null; productVersion = $null }
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path)) {
        return $info
    }
    try {
        $vi = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($Path)
        $info.fileVersion = $vi.FileVersion
        $info.productVersion = $vi.ProductVersion
    }
    catch {
        $info.error = $_.Exception.Message
    }
    return $info
}

function Get-OsInfo {
    $info = [ordered]@{
        os = [Environment]::OSVersion.ToString()
        platform = [Environment]::OSVersion.Platform.ToString()
        isWindows = [bool](Test-IsWindowsOs)
        psVersion = $PSVersionTable.PSVersion.ToString()
        userInteractive = [Environment]::UserInteractive
        sessionName = [string]$env:SESSIONNAME
    }
    if (-not (Test-IsWindowsOs)) { return $info }
    try {
        $os = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
        $info.caption = $os.Caption
        $info.build = $os.BuildNumber
        $info.version = $os.Version
    }
    catch {
        $info.cimError = $_.Exception.Message
    }
    return $info
}

function Get-MonitorInfo {
    $monitors = New-Object System.Collections.ArrayList
    if (-not (Test-IsWindowsOs)) { return @($monitors) }
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
        $index = 0
        foreach ($screen in [System.Windows.Forms.Screen]::AllScreens) {
            $index++
            [void]$monitors.Add([ordered]@{
                index = $index
                primary = [bool]$screen.Primary
                bounds = ('{0}x{1}+{2}+{3}' -f $screen.Bounds.Width, $screen.Bounds.Height, $screen.Bounds.X, $screen.Bounds.Y)
                workingArea = ('{0}x{1}' -f $screen.WorkingArea.Width, $screen.WorkingArea.Height)
                bitsPerPixel = $screen.BitsPerPixel
                deviceName = [string]$screen.DeviceName
            })
        }
    }
    catch { }
    try {
        foreach ($cim in @(Get-CimInstance -ClassName Win32_DesktopMonitor -ErrorAction Stop)) {
            if ([string]::IsNullOrWhiteSpace([string]$cim.Name)) { continue }
            [void]$monitors.Add([ordered]@{
                source = 'Win32_DesktopMonitor'
                name = [string]$cim.Name
                screenWidth = $cim.ScreenWidth
                screenHeight = $cim.ScreenHeight
            })
        }
    }
    catch { }
    try {
        $logPixels = (Get-ItemProperty -Path 'HKCU:\Control Panel\Desktop' -Name 'LogPixels' -ErrorAction SilentlyContinue).LogPixels
        if ($null -ne $logPixels) {
            [void]$monitors.Add([ordered]@{
                source = 'HKCU LogPixels'
                logPixels = [int]$logPixels
                scalingPercent = [int][Math]::Round((([int]$logPixels) / 96.0) * 100)
            })
        }
    }
    catch { }
    return @($monitors)
}

function Get-VolumeInfo {
    $volumes = New-Object System.Collections.ArrayList
    if (-not (Test-IsWindowsOs)) { return @($volumes) }
    try {
        foreach ($disk in @(Get-CimInstance -ClassName Win32_LogicalDisk -ErrorAction Stop)) {
            [void]$volumes.Add([ordered]@{
                deviceId = [string]$disk.DeviceID
                driveType = [int]$disk.DriveType
                fileSystem = [string]$disk.FileSystem
                freeSpace = $disk.FreeSpace
                size = $disk.Size
                volumeName = [string]$disk.VolumeName
            })
        }
    }
    catch { }
    return @($volumes)
}

function Get-GpuInfo {
    $gpus = New-Object System.Collections.ArrayList
    if (-not (Test-IsWindowsOs)) { return @($gpus) }
    try {
        foreach ($gpu in @(Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop)) {
            [void]$gpus.Add([ordered]@{
                name = [string]$gpu.Name
                driverVersion = [string]$gpu.DriverVersion
                driverDate = [string]$gpu.DriverDate
                adapterRam = $gpu.AdapterRAM
            })
        }
    }
    catch { }
    return @($gpus)
}

function Get-LongPathFlag {
    $info = [ordered]@{ available = $false; enabled = $null }
    if (-not (Test-IsWindowsOs)) { return $info }
    try {
        $value = (Get-ItemProperty -Path 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' -Name 'LongPathsEnabled' -ErrorAction Stop).LongPathsEnabled
        $info.available = $true
        $info.enabled = [int]$value -eq 1
        $info.raw = $value
    }
    catch {
        $info.error = $_.Exception.Message
    }
    return $info
}

function Get-ReleaseArtifactInfo {
    param([string]$Root)
    $info = [ordered]@{ portableZip = $null; installer = $null }
    $dist = Join-Path $Root 'dist'
    if (-not (Test-Path -LiteralPath $dist)) { return $info }
    $zip = @(Get-ChildItem -LiteralPath $dist -Filter 'MViewer-*-portable.zip' -ErrorAction SilentlyContinue | Select-Object -First 1)
    $setup = @(Get-ChildItem -LiteralPath $dist -Filter 'MViewer-*-Setup.exe' -ErrorAction SilentlyContinue | Select-Object -First 1)
    if ($zip.Count -gt 0) { $info.portableZip = $zip[0].FullName }
    if ($setup.Count -gt 0) { $info.installer = $setup[0].FullName }
    return $info
}

function Test-InteractiveSession {
    if (-not [Environment]::UserInteractive) { return $false }
    if (-not (Test-IsWindowsOs)) { return $false }
    $name = [string]$env:SESSIONNAME
    if ($name -eq 'Services') { return $false }
    return $true
}

function Get-EnvironmentInfo {
    param([string]$Root, [string]$ExePath)
    $volumes = @(Get-VolumeInfo)
    $writable = @($volumes | Where-Object {
        $_.driveType -eq 3 -and $null -ne $_.freeSpace -and ([int64]$_.freeSpace) -gt 0
    })
    $monitors = @(Get-MonitorInfo)
    $screenCount = @($monitors | Where-Object { $null -ne $_.bounds }).Count
    return [ordered]@{
        collectedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
        os = Get-OsInfo
        monitors = $monitors
        screenCount = $screenCount
        gpus = @(Get-GpuInfo)
        volumes = $volumes
        writableVolumeCount = $writable.Count
        longPaths = Get-LongPathFlag
        interactiveSession = [bool](Test-InteractiveSession)
        releaseArtifacts = Get-ReleaseArtifactInfo $Root
        mviewer = Get-ExeVersionInfo $ExePath
    }
}

function Test-NamedProbe {
    param([string]$Name, $Environment)
    switch ($Name) {
        'windows' {
            if ($Environment.os.isWindows) { return $null }
            return 'not running on Windows'
        }
        'interactiveSession' {
            if ($Environment.interactiveSession) { return $null }
            return 'no interactive Windows desktop session'
        }
        'twoMonitors' {
            if ([int]$Environment.screenCount -ge 2) { return $null }
            return "two physical displays required (detected screens: $($Environment.screenCount))"
        }
        'twoWritableVolumes' {
            if ([int]$Environment.writableVolumeCount -ge 2) { return $null }
            return "two writable physical volumes required (detected: $($Environment.writableVolumeCount))"
        }
        'longPathsEnabled' {
            $flag = $Environment.longPaths
            if ($flag.enabled -eq $true) { return $null }
            if ($flag.available -eq $false) { return 'long-path registry flag not readable' }
            return 'OS long-path policy is not enabled (LongPathsEnabled=0)'
        }
        'releaseArtifacts' {
            $art = $Environment.releaseArtifacts
            if ($art.portableZip -and $art.installer) { return $null }
            return 'shipping portable ZIP and NSIS installer were not found under dist/'
        }
        default { return $null }
    }
}

function Get-MissingProbeReasons {
    param($Item, $Environment)
    $reasons = New-Object System.Collections.Generic.List[string]
    foreach ($probe in @(Get-PropertyValue $Item 'probes')) {
        $reason = Test-NamedProbe ([string]$probe) $Environment
        if ($reason) { [void]$reasons.Add($reason) }
    }
    return @($reasons)
}

function New-ResultSlot {
    param($Item)
    return [ordered]@{
        id = [string]$Item.id
        title = [string]$Item.title
        result = 'NOT_RUN'
        note = ''
        screenshot = $null
        blockedReason = $null
        recordedUtc = $null
    }
}

function Read-ResumeDocument {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path)) { return $null }
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "resume file not found: $Path"
    }
    $text = Read-Utf8Text $Path
    try {
        return ($text | ConvertFrom-Json)
    }
    catch {
        throw "resume file is not valid JSON: $($_.Exception.Message)"
    }
}

function Merge-ResumeSlots {
    param($Slots, $ResumeDoc)
    if ($null -eq $ResumeDoc) { return }
    $previous = @{}
    foreach ($row in @(Get-PropertyValue $ResumeDoc 'items')) {
        $id = [string](Get-PropertyValue $row 'id')
        if ($id) { $previous[$id] = $row }
    }
    foreach ($slot in $Slots) {
        if (-not $previous.ContainsKey($slot.id)) { continue }
        $old = $previous[$slot.id]
        $result = [string](Get-PropertyValue $old 'result')
        if ([string]::IsNullOrWhiteSpace($result) -or $result -eq 'NOT_RUN') { continue }
        $slot.result = $result
        $slot.note = [string](Get-PropertyValue $old 'note')
        $slot.screenshot = Get-PropertyValue $old 'screenshot'
        $slot.blockedReason = Get-PropertyValue $old 'blockedReason'
        $slot.recordedUtc = Get-PropertyValue $old 'recordedUtc'
    }
}

function ConvertTo-JsonDocument {
    param($Object)
    return ($Object | ConvertTo-Json -Depth 8)
}

function Save-Session {
    param($State)
    $State.updatedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    $jsonPath = Join-Path $State.outDir 'results.json'
    $mdPath = Join-Path $State.outDir 'report.md'
    Write-Utf8NoBom $jsonPath (ConvertTo-JsonDocument $State)
    Write-Utf8NoBom $mdPath (Format-MarkdownReport $State)
    Write-Utf8NoBom (Join-Path $State.outDir 'environment.json') (ConvertTo-JsonDocument $State.environment)
}

function Escape-MarkdownCell {
    param([string]$Text)
    if ($null -eq $Text) { return '' }
    return (($Text -replace '\r?\n', ' ') -replace '\|', '/')
}

function Format-MarkdownReport {
    param($State)
    $lines = New-Object System.Collections.Generic.List[string]
    [void]$lines.Add('# Manual acceptance results')
    [void]$lines.Add('')
    [void]$lines.Add(('Started (UTC): {0}' -f $State.startedUtc))
    [void]$lines.Add(('Updated (UTC): {0}' -f $State.updatedUtc))
    [void]$lines.Add(('Host: {0}' -f $State.environment.os.os))
    $exePath = $State.environment.mviewer.path
    $ver = $State.environment.mviewer.productVersion
    if (-not $ver) { $ver = $State.environment.mviewer.fileVersion }
    [void]$lines.Add(('MViewer: {0} ({1})' -f $(if ($exePath) { $exePath } else { '(not found)' }), $(if ($ver) { $ver } else { 'version unknown' })))
    [void]$lines.Add(('Checklist: {0}' -f $State.checklist))
    [void]$lines.Add('')
    [void]$lines.Add('## Environment')
    [void]$lines.Add('')
    [void]$lines.Add(('- Windows: {0}' -f $State.environment.os.isWindows))
    [void]$lines.Add(('- Interactive session: {0}' -f $State.environment.interactiveSession))
    [void]$lines.Add(('- Screens: {0}' -f $State.environment.screenCount))
    [void]$lines.Add(('- Writable volumes: {0}' -f $State.environment.writableVolumeCount))
    $lp = $State.environment.longPaths.enabled
    [void]$lines.Add(('- Long paths enabled: {0}' -f $(if ($null -eq $lp) { 'unknown' } else { $lp })))
    $gpuNames = @($State.environment.gpus | ForEach-Object { $_.name } | Where-Object { $_ })
    [void]$lines.Add(('- GPU: {0}' -f $(if ($gpuNames.Count) { ($gpuNames -join '; ') } else { 'unknown' })))
    [void]$lines.Add('')
    [void]$lines.Add('## Results')
    [void]$lines.Add('')
    [void]$lines.Add('| # | Item | Result | Evidence |')
    [void]$lines.Add('|---|------|--------|----------|')
    $n = 0
    foreach ($slot in @($State.items)) {
        $n++
        $evidence = New-Object System.Collections.Generic.List[string]
        if ($slot.note) { [void]$evidence.Add($slot.note) }
        if ($slot.blockedReason) { [void]$evidence.Add(('blocked: {0}' -f $slot.blockedReason)) }
        if ($slot.screenshot) { [void]$evidence.Add(('screenshot: {0}' -f $slot.screenshot)) }
        if ($evidence.Count -eq 0) { [void]$evidence.Add('') }
        $itemText = '{0} {1}' -f $slot.id, $slot.title
        [void]$lines.Add(('| {0} | {1} | {2} | {3} |' -f $n, (Escape-MarkdownCell $itemText), $slot.result, (Escape-MarkdownCell ($evidence -join '; '))))
    }
    [void]$lines.Add('')
    [void]$lines.Add('Attach this report plus `results.json` and any screenshots to the source review document before claiming native hardware sign-off.')
    [void]$lines.Add('')
    return ($lines -join "`n")
}

function Write-ItemBanner {
    param($Item, [int]$Position, [int]$Total, [string[]]$Missing)
    Write-Host ''
    Write-Host ('=== [{0}/{1}] {2}: {3} ===' -f $Position, $Total, $Item.id, $Item.title)
    $src = $Item.source
    Write-Host ('Source: {0} (row {1})' -f $src.report, $src.row)
    foreach ($extra in @(Get-PropertyValue $Item 'also')) {
        Write-Host ('  also: {0} ({1})' -f $extra.report, $extra.row)
    }
    Write-Host 'Prerequisites:'
    foreach ($p in @(Get-PropertyValue $Item 'prerequisites')) {
        Write-Host ('  - {0}' -f $p)
    }
    Write-Host 'Steps:'
    $stepN = 0
    foreach ($step in @(Get-PropertyValue $Item 'steps')) {
        $stepN++
        Write-Host ('  {0}. {1}' -f $stepN, $step)
    }
    Write-Host ('Expected: {0}' -f $Item.expected)
    $hints = @(Get-PropertyValue $Item 'automationHints')
    if ($hints.Count -gt 0) {
        Write-Host 'Automation hints:'
        foreach ($hint in $hints) { Write-Host ('  - {0}' -f $hint) }
    }
    if ($Missing.Count -gt 0) {
        Write-Host 'Detected missing prerequisites:'
        foreach ($reason in $Missing) { Write-Host ('  - {0}' -f $reason) }
    }
}

function Read-PromptLine {
    param([string]$Message, [string]$Default = '')
    if ($script:Quiet) { return $Default }
    $shown = $Message
    if ($Default -ne '') { $shown = '{0} [{1}]' -f $Message, $Default }
    $reply = Read-Host $shown
    if ([string]::IsNullOrWhiteSpace($reply)) { return $Default }
    return $reply.Trim()
}

function Read-YesNo {
    param([string]$Message, [bool]$DefaultYes)
    $default = 'N'
    if ($DefaultYes) { $default = 'Y' }
    $reply = Read-PromptLine $Message $default
    return ($reply -match '^(Y|Yes)$')
}

function Read-Verdict {
    while ($true) {
        $reply = Read-PromptLine 'Result  P=Pass  F=Fail  B=Blocked  S=Skip  Q=Quit' 'P'
        switch -Regex ($reply) {
            '^(P|Pass)$' { return 'PASS' }
            '^(F|Fail)$' { return 'FAIL' }
            '^(B|Blocked)$' { return 'BLOCKED' }
            '^(S|Skip)$' { return 'SKIP' }
            '^(Q|Quit)$' { return 'QUIT' }
            default { Write-Host 'Enter P, F, B, S, or Q.' }
        }
    }
}

function Start-MViewerProcess {
    param([string]$ExePath, [string]$Folder)
    if ([string]::IsNullOrWhiteSpace($ExePath) -or -not (Test-Path -LiteralPath $ExePath)) {
        Write-Host 'MViewer.exe is not available to launch.'
        return
    }
    $args = @()
    if (-not [string]::IsNullOrWhiteSpace($Folder)) { $args += $Folder }
    try {
        if ($args.Count -gt 0) {
            Start-Process -FilePath $ExePath -ArgumentList $args | Out-Null
        }
        else {
            Start-Process -FilePath $ExePath | Out-Null
        }
        Write-Host ('Launched {0}' -f $ExePath)
    }
    catch {
        Write-Host ('Failed to launch MViewer: {0}' -f $_.Exception.Message)
    }
}

function Save-PrimaryScreenshot {
    param([string]$OutDir, [string]$Id)
    $shotDir = Join-Path $OutDir 'screenshots'
    if (-not (Test-Path -LiteralPath $shotDir)) {
        New-Item -ItemType Directory -Path $shotDir -Force | Out-Null
    }
    $safe = ($Id -replace '[^A-Za-z0-9._-]', '_')
    $path = Join-Path $shotDir ($safe + '.png')
    try {
        Add-Type -AssemblyName System.Windows.Forms -ErrorAction Stop
        Add-Type -AssemblyName System.Drawing -ErrorAction Stop
        $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
        $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
        $graphics = [System.Drawing.Graphics]::FromImage($bmp)
        $graphics.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose()
        $bmp.Dispose()
        return $path
    }
    catch {
        Write-Host ('Screenshot failed: {0}' -f $_.Exception.Message)
        return $null
    }
}

function Get-LaunchFlag {
    param($Item)
    $value = Get-PropertyValue $Item 'launchViewer'
    if ($null -eq $value) { return $true }
    return [bool]$value
}

function Invoke-WizardItem {
    param($Item, $Slot, $State, [int]$Position, [int]$Total)
    $missing = @(Get-MissingProbeReasons $Item $State.environment)
    Write-ItemBanner $Item $Position $Total $missing
    if ($script:Quiet) { return 'CONTINUE' }

    if ($missing.Count -gt 0) {
        $reason = $missing -join '; '
        if (Read-YesNo 'Mark BLOCKED with the detected reason?' $true) {
            $Slot.result = 'BLOCKED'
            $Slot.blockedReason = $reason
            $Slot.note = Read-PromptLine 'Note (optional)' $reason
            $Slot.recordedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
            return 'CONTINUE'
        }
    }

    if ((Get-LaunchFlag $Item) -and (Read-YesNo 'Launch MViewer?' $true)) {
        Start-MViewerProcess $State.exe $State.sampleDir
    }

    $verdict = Read-Verdict
    if ($verdict -eq 'QUIT') { return 'QUIT' }
    $Slot.result = $verdict
    $Slot.note = Read-PromptLine 'Note (optional)' ''
    if ($verdict -eq 'BLOCKED' -and $missing.Count -gt 0 -and [string]::IsNullOrWhiteSpace($Slot.note)) {
        $Slot.blockedReason = ($missing -join '; ')
    }
    if (Read-YesNo 'Capture a screenshot of the primary screen?' $false) {
        $shot = Save-PrimaryScreenshot $State.outDir $Item.id
        if ($shot) {
            $Slot.screenshot = $shot
            Write-Host ('Saved {0}' -f $shot)
        }
    }
    $Slot.recordedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    return 'CONTINUE'
}

function Show-ChecklistList {
    param($Items)
    $n = 0
    foreach ($item in $Items) {
        $n++
        $group = [string](Get-PropertyValue $item 'group')
        Write-Host ('{0,3}. {1,-14} {2,-12} {3}' -f $n, $item.id, $group, $item.title)
        Write-Host ('     {0} row {1}' -f $item.source.report, $item.source.row)
    }
}

function Select-ActiveItems {
    param($Items, [string[]]$Wanted)
    if ($Wanted.Count -eq 0) { return @($Items) }
    $byId = @{}
    foreach ($item in $Items) { $byId[[string]$item.id] = $item }
    $selected = New-Object System.Collections.ArrayList
    foreach ($id in $Wanted) {
        if (-not $byId.ContainsKey($id)) {
            throw "unknown checklist id '$id'"
        }
        [void]$selected.Add($byId[$id])
    }
    return @($selected)
}

function Get-RunExitCode {
    param($Slots)
    $executed = @($Slots | Where-Object { $_.result -ne 'NOT_RUN' })
    if (@($executed | Where-Object { $_.result -eq 'FAIL' }).Count -gt 0) { return 1 }
    return 0
}

function Resolve-LocatedExe {
    param([string]$Root, [string]$Explicit)
    try {
        $found = Find-MViewerExe $Root $Explicit
    }
    catch {
        if (-not $script:Quiet) { throw }
        Write-Host ('WARN: {0}' -f $_.Exception.Message)
        $found = $null
    }
    if (-not $found) {
        Write-Host 'WARN: MViewer.exe not found (pass -Exe or build a Release/portable package).'
    }
    return $found
}

function Resolve-SampleDirectory {
    param([string]$Requested)
    if ([string]::IsNullOrWhiteSpace($Requested)) { return $Requested }
    if ([IO.Path]::IsPathRooted($Requested)) { return $Requested }
    $fromCwd = Join-Path (Get-Location).Path $Requested
    if (Test-Path -LiteralPath $fromCwd) {
        return (Resolve-Path -LiteralPath $fromCwd).Path
    }
    return $Requested
}

function Write-SessionBanner {
    param($State, $Items)
    Write-Host ('Checklist: {0} ({1} items)' -f $State.checklist, $Items.Count)
    Write-Host ('OutDir:    {0}' -f $State.outDir)
    if ($State.exe) { Write-Host ('Exe:       {0}' -f $State.exe) }
    Write-Host ('OS:        {0}' -f $State.environment.os.os)
    Write-Host ('Screens:   {0}' -f $State.environment.screenCount)
    Write-Host ('Volumes:   {0} writable' -f $State.environment.writableVolumeCount)
}

function Invoke-QuietValidation {
    param($State, $Items)
    Write-Host 'Mode:      DryRun/NonInteractive (no prompts)'
    foreach ($item in $Items) {
        $missing = @(Get-MissingProbeReasons $item $State.environment)
        if ($missing.Count -gt 0) {
            Write-Host ('  {0}: would BLOCK ({1})' -f $item.id, ($missing -join '; '))
        }
        else {
            Write-Host ('  {0}: probes ok' -f $item.id)
        }
    }
    Save-Session $State
    Write-Host 'Checklist and environment validated.'
    return 0
}

function Invoke-InteractiveSession {
    param($State, $Items, $Slots)
    Save-Session $State
    $position = 0
    foreach ($item in $Items) {
        $position++
        $slot = $Slots[$position - 1]
        if ($slot.result -ne 'NOT_RUN') {
            Write-Host ('Skipping {0} (already {1})' -f $slot.id, $slot.result)
            continue
        }
        $action = Invoke-WizardItem $item $slot $State $position $Items.Count
        Save-Session $State
        if ($action -eq 'QUIT') { break }
    }
    Write-Host ''
    Write-Host ('Saved {0}' -f (Join-Path $State.outDir 'results.json'))
    Write-Host ('Saved {0}' -f (Join-Path $State.outDir 'report.md'))
    return (Get-RunExitCode $Slots)
}

function Invoke-ManualAcceptance {
    $root = $RepoRoot
    if ([string]::IsNullOrWhiteSpace($root)) { $root = Get-DefaultRepoRoot }
    $root = (Resolve-Path -LiteralPath $root).Path

    $checklistPath = Resolve-ChecklistFile $root $Checklist
    if (-not (Test-Path -LiteralPath $checklistPath)) {
        throw "checklist not found: $checklistPath"
    }
    $checklistPath = (Resolve-Path -LiteralPath $checklistPath).Path

    $document = Read-ChecklistDocument $checklistPath
    $validation = @(Test-ChecklistDocument $document $root)
    if ($validation.Count -gt 0) {
        foreach ($err in $validation) { Write-Host ('FAIL: {0}' -f $err) }
        return 2
    }

    $items = @(Select-ActiveItems @($document.items) @(Parse-OnlyIds $Only))
    if ($List) {
        Show-ChecklistList $items
        return 0
    }

    $exePath = Resolve-LocatedExe $root $Exe
    $resolvedOut = Resolve-OutputDirectory $root $OutDir $Resume
    if (-not (Test-Path -LiteralPath $resolvedOut)) {
        New-Item -ItemType Directory -Path $resolvedOut -Force | Out-Null
    }
    $resolvedOut = (Resolve-Path -LiteralPath $resolvedOut).Path

    $slots = @($items | ForEach-Object { New-ResultSlot $_ })
    if ($Resume) { Merge-ResumeSlots $slots (Read-ResumeDocument $Resume) }

    $state = [ordered]@{
        schema = 'manual-acceptance-results/v1'
        startedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
        updatedUtc = $null
        checklist = $checklistPath
        outDir = $resolvedOut
        exe = $exePath
        sampleDir = Resolve-SampleDirectory $SampleDir
        dryRun = [bool]$DryRun
        nonInteractive = [bool]$NonInteractive
        environment = (Get-EnvironmentInfo $root $exePath)
        items = $slots
    }

    Write-SessionBanner $state $items
    if ($script:Quiet) { return (Invoke-QuietValidation $state $items) }
    return (Invoke-InteractiveSession $state $items $slots)
}

try {
    $code = Invoke-ManualAcceptance
    exit $code
}
catch {
    Write-Host ('ERROR: {0}' -f $_.Exception.Message)
    if ($_.ScriptStackTrace) { Write-Host $_.ScriptStackTrace }
    exit 2
}
