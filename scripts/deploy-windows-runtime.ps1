[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [Parameter(Mandatory = $true)]
    [string]$QtPrefix,

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Find-DumpBin {
    $command = Get-Command "dumpbin" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $visualStudioRoot = Join-Path ${env:ProgramFiles} "Microsoft Visual Studio\2022"
    if (Test-Path -LiteralPath $visualStudioRoot -PathType Container) {
        $candidate = Get-ChildItem `
            -Path (Join-Path $visualStudioRoot "*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe") `
            -File -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending |
            Select-Object -First 1
        if ($null -ne $candidate) {
            return $candidate.FullName
        }
    }
    throw "dumpbin.exe was not found. Install the VS2022 C++ desktop workload."
}

function Copy-QtDependencies {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$InitialBinaries,

        [Parameter(Mandatory = $true)]
        [string]$QtBinDirectory,

        [Parameter(Mandatory = $true)]
        [string]$DestinationDirectory,

        [Parameter(Mandatory = $true)]
        [string]$DumpBin
    )

    $pending = New-Object 'System.Collections.Generic.Queue[string]'
    foreach ($binary in $InitialBinaries) {
        $pending.Enqueue($binary)
    }
    $visited = @{}

    while ($pending.Count -gt 0) {
        $binary = $pending.Dequeue()
        $binaryKey = [System.IO.Path]::GetFullPath($binary).ToLowerInvariant()
        if ($visited.ContainsKey($binaryKey)) {
            continue
        }
        $visited[$binaryKey] = $true

        $dependencyOutput = & $DumpBin /nologo /dependents $binary 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "dumpbin could not inspect '$binary'."
        }

        foreach ($line in $dependencyOutput) {
            if ($line -notmatch '^\s+([^\s]+\.dll)\s*$') {
                continue
            }

            $dependencyName = $Matches[1]
            $dependencySource = Join-Path $QtBinDirectory $dependencyName
            if (-not (Test-Path -LiteralPath $dependencySource -PathType Leaf)) {
                # The remaining dependencies are supplied by Windows.
                continue
            }

            $dependencyDestination = Join-Path $DestinationDirectory $dependencyName
            Copy-Item -LiteralPath $dependencySource `
                -Destination $dependencyDestination -Force
            $pending.Enqueue($dependencyDestination)
        }
    }
}

$executablePath = [System.IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "Executable was not found: $executablePath"
}

$qtRoot = [System.IO.Path]::GetFullPath($QtPrefix)
$qtBinDirectory = Join-Path $qtRoot "bin"
$qmake = @("qmake6.exe", "qmake.exe") |
    ForEach-Object { Join-Path $qtBinDirectory $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $qmake) {
    throw "qmake was not found under '$qtBinDirectory'."
}

$qtPluginDirectory = (& $qmake -query QT_INSTALL_PLUGINS).Trim()
if ($LASTEXITCODE -ne 0 -or -not $qtPluginDirectory) {
    throw "Unable to query the Qt plugin directory."
}

$destinationDirectory = Split-Path -Parent $executablePath
$platformFileName = "qwindows.dll"
if ($Configuration -eq "Debug") {
    $debugPlatform = Join-Path $qtPluginDirectory "platforms\qwindowsd.dll"
    if (Test-Path -LiteralPath $debugPlatform -PathType Leaf) {
        $platformFileName = "qwindowsd.dll"
    }
}

$platformSource = Join-Path $qtPluginDirectory "platforms\$platformFileName"
if (-not (Test-Path -LiteralPath $platformSource -PathType Leaf)) {
    throw "Qt Windows platform plugin was not found: $platformSource"
}

$platformDestination = Join-Path $destinationDirectory "platforms"
New-Item -ItemType Directory -Path $platformDestination -Force | Out-Null
$deployedPlatform = Join-Path $platformDestination $platformFileName
Copy-Item -LiteralPath $platformSource -Destination $deployedPlatform -Force

$dumpBin = Find-DumpBin
Copy-QtDependencies `
    -InitialBinaries @($executablePath, $deployedPlatform) `
    -QtBinDirectory $qtBinDirectory `
    -DestinationDirectory $destinationDirectory `
    -DumpBin $dumpBin

Set-Content -LiteralPath (Join-Path $destinationDirectory "qt.conf") `
    -Value "[Paths]`r`nPlugins=." -Encoding Ascii

Write-Host "Qt development runtime deployed to: $destinationDirectory"
