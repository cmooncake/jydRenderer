[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$OutputDirectory = "dist",

    [string]$QtPrefix = "",

    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Invoke-NativeCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [Parameter(Mandatory = $true)]
        [string[]]$ArgumentList
    )

    Write-Host "> $FilePath $($ArgumentList -join ' ')"
    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $FilePath"
    }
}

function Get-FullPathFromRoot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Root
    )

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Root $Path))
}

function Find-QMake {
    param([string]$RequestedPrefix)

    if ($RequestedPrefix) {
        foreach ($name in @("qmake6.exe", "qmake.exe")) {
            $candidate = Join-Path (Join-Path $RequestedPrefix "bin") $name
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
        throw "qmake was not found under '$RequestedPrefix\bin'."
    }

    foreach ($name in @("qmake6", "qmake")) {
        $command = Get-Command $name -ErrorAction SilentlyContinue
        if ($null -ne $command) {
            return $command.Source
        }
    }
    throw "Qt was not found. Add qmake/qmake6 to PATH or pass -QtPrefix."
}

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
    return $null
}

function Copy-LocalDllDependencies {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$InitialBinaries,

        [Parameter(Mandatory = $true)]
        [string]$SearchDirectory,

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
            $dependencySource = Join-Path $SearchDirectory $dependencyName
            if (-not (Test-Path -LiteralPath $dependencySource -PathType Leaf)) {
                # Windows system DLLs are intentionally not copied.
                continue
            }

            $dependencyDestination = Join-Path $DestinationDirectory $dependencyName
            if (-not (Test-Path -LiteralPath $dependencyDestination -PathType Leaf)) {
                Copy-Item -LiteralPath $dependencySource `
                    -Destination $dependencyDestination -Force
            }
            $pending.Enqueue($dependencyDestination)
        }
    }
}

function Repair-BrokenSdlFetchContentCache {
    param(
        [Parameter(Mandatory = $true)]
        [string]$RepositoryRoot
    )

    $buildRoot = [System.IO.Path]::GetFullPath(
        (Join-Path $RepositoryRoot "build\vs2022-x64"))
    $sdlSource = [System.IO.Path]::GetFullPath(
        (Join-Path $buildRoot "_deps\sdl2-src"))
    $sdlCMakeFile = Join-Path $sdlSource "CMakeLists.txt"

    $cacheNeedsReset = $false
    $cacheFile = Join-Path $buildRoot "CMakeCache.txt"
    if (Test-Path -LiteralPath $cacheFile -PathType Leaf) {
        $cacheNeedsReset = [bool](Select-String -LiteralPath $cacheFile `
            -Pattern '^FETCHCONTENT_FULLY_DISCONNECTED:BOOL=ON$|^FETCHCONTENT_SOURCE_DIR_SDL2:' `
            -Quiet)
    }

    $sourceDirectoryExists = Test-Path -LiteralPath $sdlSource -PathType Container
    $sourceIsValid = Test-Path -LiteralPath $sdlCMakeFile -PathType Leaf
    if ($sourceIsValid -or (-not $sourceDirectoryExists -and -not $cacheNeedsReset)) {
        return $false
    }

    Write-Warning "Incomplete SDL2 FetchContent cache detected; repairing it."
    $safePrefix = $buildRoot.TrimEnd('\', '/') +
        [System.IO.Path]::DirectorySeparatorChar
    foreach ($relativePath in @("_deps\sdl2-src", "_deps\sdl2-subbuild")) {
        $target = [System.IO.Path]::GetFullPath((Join-Path $buildRoot $relativePath))
        if (-not $target.StartsWith(
                $safePrefix,
                [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Unsafe SDL2 cache path: $target"
        }
        if (Test-Path -LiteralPath $target) {
            Remove-Item -LiteralPath $target -Recurse -Force
        }
    }
    return $true
}

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputRoot = Get-FullPathFromRoot -Path $OutputDirectory -Root $repositoryRoot
$packageName = "jydRenderer-windows-x64"
$stagingDirectory = [System.IO.Path]::GetFullPath((Join-Path $outputRoot $packageName))
$zipPath = [System.IO.Path]::GetFullPath((Join-Path $outputRoot "${packageName}.zip"))

# The script only removes the fixed package directory and zip below the chosen
# output directory. Never allow either target to resolve to the output root.
$outputPrefix = $outputRoot.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
if (-not $stagingDirectory.StartsWith($outputPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    -not $zipPath.StartsWith($outputPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe package output path."
}

$qmake = Find-QMake -RequestedPrefix $QtPrefix
$detectedQtPrefix = (& $qmake -query QT_INSTALL_PREFIX).Trim()
$qtVersion = (& $qmake -query QT_VERSION).Trim()
$qtPluginDirectory = (& $qmake -query QT_INSTALL_PLUGINS).Trim()
if ($LASTEXITCODE -ne 0 -or -not $detectedQtPrefix -or -not $qtVersion) {
    throw "Unable to query the Qt installation with '$qmake'."
}

$qtMajor = $qtVersion.Split('.')[0]
$qtBinDirectory = Join-Path $detectedQtPrefix "bin"
$buildPreset = "vs2022-x64-$($Configuration.ToLowerInvariant())"
$executable = Join-Path $repositoryRoot "build\vs2022-x64\$Configuration\jydRenderer.exe"

Push-Location $repositoryRoot
try {
    if (-not $SkipBuild) {
        $repairedSdlCache = Repair-BrokenSdlFetchContentCache `
            -RepositoryRoot $repositoryRoot
        $configureArguments = @(
            "--preset", "vs2022-x64",
            "-DCMAKE_PREFIX_PATH=$detectedQtPrefix"
        )
        if ($repairedSdlCache) {
            # Remove diagnostic/offline overrides that would prevent
            # FetchContent from repopulating the repaired source directory.
            $configureArguments += "-UFETCHCONTENT_FULLY_DISCONNECTED"
            $configureArguments += "-UFETCHCONTENT_SOURCE_DIR_SDL2"
        }
        Invoke-NativeCommand -FilePath "cmake" `
            -ArgumentList $configureArguments
        Invoke-NativeCommand -FilePath "cmake" -ArgumentList @(
            "--build", "--preset", $buildPreset
        )
    }

    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
        throw "Built executable was not found: $executable"
    }

    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    New-Item -ItemType Directory -Path $stagingDirectory -Force | Out-Null

    Copy-Item -LiteralPath $executable -Destination $stagingDirectory

    $windeployqt = Join-Path $qtBinDirectory "windeployqt.exe"
    if (-not (Test-Path -LiteralPath $windeployqt -PathType Leaf)) {
        $deployCommand = Get-Command "windeployqt" -ErrorAction SilentlyContinue
        if ($null -eq $deployCommand) {
            throw "windeployqt.exe was not found in '$qtBinDirectory' or PATH."
        }
        $windeployqt = $deployCommand.Source
    }

    $deployMode = if ($Configuration -eq "Debug") { "--debug" } else { "--release" }
    $deployArguments = @(
        $deployMode,
        "--compiler-runtime",
        "--no-translations",
        "--dir", $stagingDirectory,
        $executable
    )
    Write-Host "> $windeployqt $($deployArguments -join ' ')"
    & $windeployqt @deployArguments
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "windeployqt failed; using the local DLL dependency fallback."
    }

    # Some package-manager Qt distributions use suffixed DLL names and their
    # windeployqt cannot locate the platforms directory. Ensure the three
    # directly used modules and qwindows.dll are present as a portable fallback.
    foreach ($module in @("Core", "Gui", "Widgets")) {
        $pattern = "Qt${qtMajor}${module}*.dll"
        $candidates = Get-ChildItem -LiteralPath $qtBinDirectory -Filter $pattern -File
        if ($Configuration -eq "Release") {
            $debugName = "^Qt${qtMajor}${module}d\.dll$"
            $candidates = $candidates | Where-Object { $_.Name -notmatch $debugName }
        }
        if (-not $candidates) {
            throw "Required Qt module was not found: $pattern"
        }
        foreach ($candidate in $candidates) {
            Copy-Item -LiteralPath $candidate.FullName -Destination $stagingDirectory -Force
        }
    }

    $platformFileName = "qwindows.dll"
    if ($Configuration -eq "Debug") {
        $debugPlatform = Join-Path $qtPluginDirectory "platforms\qwindowsd.dll"
        if (Test-Path -LiteralPath $debugPlatform -PathType Leaf) {
            $platformFileName = "qwindowsd.dll"
        }
    }
    $platformSource = Join-Path $qtPluginDirectory "platforms\$platformFileName"
    if (-not (Test-Path -LiteralPath $platformSource -PathType Leaf)) {
        throw "The Qt Windows platform plugin was not found: $platformSource"
    }
    $platformDestination = Join-Path $stagingDirectory "platforms"
    New-Item -ItemType Directory -Path $platformDestination -Force | Out-Null
    Copy-Item -LiteralPath $platformSource -Destination $platformDestination -Force

    $dumpBin = Find-DumpBin
    if ($null -eq $dumpBin) {
        throw "dumpbin.exe was not found; install the VS2022 C++ desktop workload."
    }
    $packagedBinaries = @(
        (Join-Path $stagingDirectory "jydRenderer.exe"),
        (Join-Path $platformDestination $platformFileName)
    )
    $packagedBinaries += Get-ChildItem -LiteralPath $stagingDirectory `
        -Filter "Qt${qtMajor}*.dll" -File |
        Select-Object -ExpandProperty FullName
    Copy-LocalDllDependencies `
        -InitialBinaries $packagedBinaries `
        -SearchDirectory $qtBinDirectory `
        -DestinationDirectory $stagingDirectory `
        -DumpBin $dumpBin

    Set-Content -LiteralPath (Join-Path $stagingDirectory "qt.conf") `
        -Value "[Paths]`r`nPlugins=." -Encoding Ascii

    Compress-Archive -Path (Join-Path $stagingDirectory "*") `
        -DestinationPath $zipPath -CompressionLevel Optimal

    Write-Host ""
    Write-Host "Windows package created successfully:" -ForegroundColor Green
    Write-Host "  Directory: $stagingDirectory"
    Write-Host "  Archive:   $zipPath"
}
finally {
    Pop-Location
}
