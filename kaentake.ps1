[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [Parameter(Position = 0)]
    [ValidateSet('setup', 'build', 'check', 'deploy')]
    [string]$Command = 'build',

    [Parameter(Position = 1)]
    [ValidateSet('debug', 'release')]
    [string]$Configuration = 'release',

    [string]$GameDir
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = $PSScriptRoot
$localConfigPath = Join-Path $repositoryRoot '.kaentake.local.json'

function Invoke-Checked {
    param(
        [Parameter(Mandatory)] [string]$FilePath,
        [Parameter(Mandatory)] [string[]]$ArgumentList
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($ArgumentList -join ' ')"
    }
}

function Get-VisualStudioGenerator {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'vswhere.exe was not found. Install Visual Studio with Desktop development with C++.'
    }

    $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json |
        ConvertFrom-Json |
        Select-Object -First 1
    if (-not $installation) {
        throw 'No Visual Studio installation with the x86/x64 C++ tools was found.'
    }

    $major = [int]($installation.installationVersion -split '\.')[0]
    $year = switch ($major) {
        17 { 2022 }
        18 { 2026 }
        default { throw "Unsupported Visual Studio major version $major. Add its CMake generator mapping." }
    }

    [PSCustomObject]@{
        Generator = "Visual Studio $major $year"
        BuildDir = Join-Path $repositoryRoot "build\local-vs$major"
    }
}

function Invoke-KaentakeBuild {
    param([ValidateSet('debug', 'release')] [string]$RequestedConfiguration)

    $visualStudio = Get-VisualStudioGenerator
    $cmakeConfiguration = (Get-Culture).TextInfo.ToTitleCase($RequestedConfiguration)
    $cache = Join-Path $visualStudio.BuildDir 'CMakeCache.txt'

    if (-not (Test-Path -LiteralPath $cache)) {
        Invoke-Checked cmake @(
            '-S', $repositoryRoot,
            '-B', $visualStudio.BuildDir,
            '-G', $visualStudio.Generator,
            '-A', 'Win32'
        )
    }

    Invoke-Checked cmake @(
        '--build', $visualStudio.BuildDir,
        '--config', $cmakeConfiguration,
        '--target', 'injector', 'launcher',
        '--parallel', '1'
    )

    [PSCustomObject]@{
        BuildDir = $visualStudio.BuildDir
        Configuration = $cmakeConfiguration
        OutputDir = Join-Path $visualStudio.BuildDir $cmakeConfiguration
    }
}

function Save-GameDirectory {
    param([Parameter(Mandatory)] [string]$Path)

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    if (-not (Test-Path -LiteralPath (Join-Path $resolved 'MapleStory.exe') -PathType Leaf)) {
        throw "MapleStory.exe was not found in '$resolved'."
    }

    @{ gameDir = $resolved } | ConvertTo-Json | Set-Content -LiteralPath $localConfigPath -Encoding utf8
    Write-Host "Saved game directory: $resolved"
}

function Get-GameDirectory {
    if (-not (Test-Path -LiteralPath $localConfigPath -PathType Leaf)) {
        throw "No game directory is configured. Run: .\kaentake.ps1 setup -GameDir 'C:\path\to\MapleStory'"
    }

    $config = Get-Content -Raw -LiteralPath $localConfigPath | ConvertFrom-Json
    $resolved = (Resolve-Path -LiteralPath $config.gameDir).Path
    if (-not (Test-Path -LiteralPath (Join-Path $resolved 'MapleStory.exe') -PathType Leaf)) {
        throw "Configured game directory '$resolved' does not contain MapleStory.exe."
    }
    return $resolved
}

function Deploy-Kaentake {
    param([ValidateSet('debug', 'release')] [string]$RequestedConfiguration)

    $gameProcesses = Get-Process -Name MapleStory -ErrorAction SilentlyContinue
    if ($gameProcesses) {
        throw 'MapleStory is running. Close it before deploying.'
    }

    $gameDirectory = Get-GameDirectory
    $build = Invoke-KaentakeBuild $RequestedConfiguration
    $artifactNames = @('Kaentake.exe', 'Kaentake.dll', 'Kaentake.pdb', 'Kaentake-launcher.pdb')
    $copied = 0

    foreach ($name in $artifactNames) {
        $source = Join-Path $build.OutputDir $name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            if ($name.EndsWith('.pdb')) { continue }
            throw "Required build artifact is missing: $source"
        }

        $destination = Join-Path $gameDirectory $name
        if ($PSCmdlet.ShouldProcess($destination, "Copy '$source'")) {
            Copy-Item -LiteralPath $source -Destination $destination -Force
            $sourceHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash
            $destinationHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash
            if ($sourceHash -ne $destinationHash) {
                throw "Hash verification failed for '$destination'."
            }
            ++$copied
        }
    }

    if ($WhatIfPreference) {
        Write-Host "Deployment dry run complete for '$gameDirectory'. Custom.wz would not be changed."
    } else {
        Write-Host "Deployment complete: $copied runtime artifact(s) copied to '$gameDirectory'. Custom.wz was not changed."
    }
}

switch ($Command) {
    'setup' {
        if ([string]::IsNullOrWhiteSpace($GameDir)) {
            throw 'setup requires -GameDir.'
        }
        Save-GameDirectory $GameDir
    }
    'build' {
        $result = Invoke-KaentakeBuild $Configuration
        Write-Host "Build complete: $($result.OutputDir)"
    }
    'check' {
        Invoke-KaentakeBuild debug | Out-Null
        Invoke-KaentakeBuild release | Out-Null
        Write-Host 'Debug and Release checks passed.'
    }
    'deploy' {
        Deploy-Kaentake $Configuration
    }
}
