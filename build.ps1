#requires -Version 7.2
[CmdletBinding()]
param(
    [string]$GameDirectory,
    [switch]$SkipGameCheck,
    [ValidatePattern('^[a-z0-9][a-z0-9-]{0,63}$')]
    [string]$PackageName = 'dungeon-slasher-r19'
)
$ErrorActionPreference = 'Stop'
if (-not $SkipGameCheck) {
    if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
        throw 'Specify -GameDirectory for the installed-game check, or -SkipGameCheck for a source-only/CI build.'
    }
    $GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path
}
$projectDirectory = $PSScriptRoot
$sourceDirectory = Join-Path $projectDirectory 'src'
$compileDirectory = Join-Path $projectDirectory "build\bin\$PackageName"
$logDirectory = Join-Path $projectDirectory 'build\logs'
$packageDirectory = Join-Path $projectDirectory "dist\$PackageName"
New-Item -ItemType Directory -Path $compileDirectory,$logDirectory,$packageDirectory -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio C++ Build Tools and Windows SDK are required.' }
$installations = @(& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
if ($LASTEXITCODE -ne 0 -or $installations.Count -eq 0) { throw 'No Visual Studio installation with x64 C++ tools was found.' }
$installation = $installations[0]
$msbuild = Join-Path $installation.installationPath 'MSBuild\Current\Bin\MSBuild.exe'
if (-not (Test-Path -LiteralPath $msbuild)) { throw 'MSBuild was not found in the selected Visual Studio installation.' }
$toolset = if ([int]($installation.installationVersion.Split('.')[0]) -ge 18) { 'v145' } else { 'v143' }
foreach ($name in @('Runtime','Trainer','Tests')) {
    & $msbuild (Join-Path $sourceDirectory "$name.vcxproj") /t:Build /p:Configuration=Release /p:Platform=x64 "/p:PlatformToolset=$toolset" "/p:TrainerPackageDir=$compileDirectory" /m:2 /nologo /v:minimal 2>&1 |
        Tee-Object -FilePath (Join-Path $logDirectory "$name-build.log")
    if ($LASTEXITCODE -ne 0) { throw "$name build failed." }
}
& (Join-Path $projectDirectory 'build\tests\DungeonSlasherTests.exe') 2>&1 |
    Tee-Object -FilePath (Join-Path $logDirectory 'tests.log')
if ($LASTEXITCODE -ne 0) { throw 'Native trainer regression tests failed.' }
if ($SkipGameCheck) {
    Write-Warning 'Installed-game hash check skipped. This build verifies source regression tests only.'
} else {
    $check = Start-Process -FilePath (Join-Path $compileDirectory 'DungeonSlasherTrainer.exe') -ArgumentList @('--check','--game-dir',('"' + $GameDirectory.TrimEnd('\') + '"')) -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $logDirectory 'game-check.log') -RedirectStandardError (Join-Path $logDirectory 'game-check.stderr.log')
    Get-Content -LiteralPath (Join-Path $logDirectory 'game-check.log') -Encoding utf8
    if ($check.ExitCode -ne 0) { throw 'Installed-game compatibility check failed.' }
}
# Package only the six runtime files. Debug symbols, INI and backups stay local.
$releaseFiles = [ordered]@{
    'DungeonSlasherTrainer.exe' = Join-Path $compileDirectory 'DungeonSlasherTrainer.exe'
    'DungeonSlasher.Runtime.dll' = Join-Path $compileDirectory 'DungeonSlasher.Runtime.dll'
    '使用说明.txt' = Join-Path $projectDirectory 'docs\使用说明.txt'
    'LICENSE.MinHook.txt' = Join-Path $projectDirectory 'third_party\minhook\LICENSE.txt'
    'LICENSE.ImGui.txt' = Join-Path $projectDirectory 'third_party\imgui\LICENSE.txt'
    'LICENSE.Project.txt' = Join-Path $projectDirectory 'LICENSE'
}
foreach ($entry in $releaseFiles.GetEnumerator()) {
    Copy-Item -LiteralPath $entry.Value -Destination (Join-Path $packageDirectory $entry.Key) -Force
}
$items = @($releaseFiles.Keys | ForEach-Object { Join-Path $packageDirectory $_ })
$zipPath = Join-Path $projectDirectory "dist\$PackageName.zip"
Compress-Archive -LiteralPath $items -DestinationPath $zipPath -CompressionLevel Optimal -Force
$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    if ($archive.Entries.Count -ne $releaseFiles.Count) { throw 'Unexpected release ZIP file count.' }
    foreach ($entry in $archive.Entries) {
        if (-not $releaseFiles.Contains($entry.FullName)) { throw "Unexpected release entry: $($entry.FullName)" }
        $stream = $entry.Open()
        $hasher = [Security.Cryptography.SHA256]::Create()
        try { $actual = [Convert]::ToHexString($hasher.ComputeHash($stream)) }
        finally { $stream.Dispose(); $hasher.Dispose() }
        $expected = (Get-FileHash -Algorithm SHA256 -LiteralPath $releaseFiles[$entry.FullName]).Hash
        if ($actual -ne $expected) { throw "Packaged bytes differ: $($entry.FullName)" }
    }
} finally { $archive.Dispose() }
$zipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zipPath).Hash.ToLowerInvariant()
[IO.File]::WriteAllText((Join-Path $projectDirectory "dist\$PackageName.sha256"),"$zipHash  $PackageName.zip`n",[Text.UTF8Encoding]::new($false))
Get-FileHash -Algorithm SHA256 -LiteralPath $zipPath | Format-List
Write-Host "Built, tested and packaged: $zipPath"
