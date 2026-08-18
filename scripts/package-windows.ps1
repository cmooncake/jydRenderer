[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$OutputDirectory = "dist",

    [string]$QtPrefix = "",

    [ValidateSet("Auto", "MSVC", "MinGW")]
    [string]$Toolchain = "Auto",

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

function Get-QtInstallation {
    param([Parameter(Mandatory = $true)][string]$QMake)

    $prefix = (& $QMake -query QT_INSTALL_PREFIX 2>$null).Trim()
    $versionText = (& $QMake -query QT_VERSION 2>$null).Trim()
    $spec = (& $QMake -query QMAKE_XSPEC 2>$null).Trim()
    $plugins = (& $QMake -query QT_INSTALL_PLUGINS 2>$null).Trim()
    if ($LASTEXITCODE -ne 0 -or -not $prefix -or -not $versionText) {
        return $null
    }

    $kind = if ($spec -match 'msvc') {
        "MSVC"
    } elseif ($spec.Contains("g++") -or $spec -match 'mingw') {
        "MinGW"
    } else {
        "Unknown"
    }
    $version = [version]"0.0"
    [void][version]::TryParse($versionText, [ref]$version)
    return [pscustomobject]@{
        QMake = [System.IO.Path]::GetFullPath($QMake)
        Prefix = [System.IO.Path]::GetFullPath($prefix)
        Version = $version
        VersionText = $versionText
        Spec = $spec
        Kind = $kind
        Plugins = [System.IO.Path]::GetFullPath($plugins)
    }
}

function Find-QtInstallation {
    param(
        [string]$RequestedPrefix,
        [string]$RequestedToolchain
    )

    $candidatePaths = @()
    if ($RequestedPrefix) {
        $candidatePaths += @(
            (Join-Path (Join-Path $RequestedPrefix "bin") "qmake6.exe"),
            (Join-Path (Join-Path $RequestedPrefix "bin") "qmake.exe")
        )
    } else {
        foreach ($name in @("qmake6", "qmake")) {
            $command = Get-Command $name -ErrorAction SilentlyContinue
            if ($null -ne $command) {
                $candidatePaths += $command.Source
            }
        }

        $roots = @()
        foreach ($environmentName in @("QTDIR", "Qt6_ROOT", "Qt5_ROOT")) {
            $value = [Environment]::GetEnvironmentVariable($environmentName)
            if ($value) {
                $roots += $value
            }
        }
        if ($env:CMAKE_PREFIX_PATH) {
            $roots += @($env:CMAKE_PREFIX_PATH -split ';')
        }
        $roots += Join-Path $env:SystemDrive "Qt"
        if ($env:USERPROFILE) {
            $roots += Join-Path $env:USERPROFILE "Qt"
        }
        if ($env:LOCALAPPDATA) {
            $roots += Join-Path $env:LOCALAPPDATA "Qt"
        }

        foreach ($root in ($roots | Where-Object { $_ } | Select-Object -Unique)) {
            if (-not (Test-Path -LiteralPath $root -PathType Container)) {
                continue
            }
            $candidatePaths += Join-Path (Join-Path $root "bin") "qmake.exe"
            foreach ($versionDirectory in (Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue)) {
                $candidatePaths += Join-Path (Join-Path $versionDirectory.FullName "bin") "qmake.exe"
                foreach ($kitDirectory in (Get-ChildItem -LiteralPath $versionDirectory.FullName -Directory -ErrorAction SilentlyContinue)) {
                    $candidatePaths += Join-Path (Join-Path $kitDirectory.FullName "bin") "qmake.exe"
                }
            }
        }
    }

    $installations = @()
    foreach ($candidate in ($candidatePaths | Select-Object -Unique)) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            continue
        }
        $installation = Get-QtInstallation -QMake $candidate
        if ($null -ne $installation -and $installation.Kind -ne "Unknown") {
            $installations += $installation
        }
    }
    if ($RequestedToolchain -ne "Auto") {
        $installations = @($installations | Where-Object { $_.Kind -eq $RequestedToolchain })
    }
    if (-not $installations) {
        throw "No compatible Qt installation was found. Install an x64 Qt MSVC or MinGW kit, add qmake to PATH, or pass -QtPrefix."
    }

    return $installations | Sort-Object @{ Expression = { if ($_.Kind -eq "MSVC") { 1 } else { 0 } }; Descending = $true }, @{ Expression = { $_.Version }; Descending = $true } | Select-Object -First 1
}

function Find-CMake {
    param([string]$QtRoot)

    $command = Get-Command "cmake" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $candidates = @()
    if ($QtRoot) {
        $candidates += Join-Path $QtRoot "Tools\CMake_64\bin\cmake.exe"
    }
    $visualStudioRoot = Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022"
    if (Test-Path -LiteralPath $visualStudioRoot -PathType Container) {
        $candidates += Get-ChildItem -Path (Join-Path $visualStudioRoot "*\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe") -File -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    throw "CMake was not found on PATH, in the Qt Tools directory, or under Visual Studio 2022."
}

function Find-Ninja {
    param([string]$QtRoot)

    $command = Get-Command "ninja" -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }
    if ($QtRoot) {
        $candidate = Join-Path $QtRoot "Tools\Ninja\ninja.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return $candidate
        }
    }
    throw "Ninja was not found on PATH or in the Qt Tools directory."
}

function Find-MinGwBinDirectory {
    param([string]$QtRoot)

    $gxx = Get-Command "g++" -ErrorAction SilentlyContinue
    if ($null -ne $gxx) {
        return Split-Path -Parent $gxx.Source
    }
    if ($QtRoot) {
        $toolsDirectory = Join-Path $QtRoot "Tools"
        if (Test-Path -LiteralPath $toolsDirectory -PathType Container) {
            $candidate = Get-ChildItem -LiteralPath $toolsDirectory -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^mingw.*_64$' } |
                Sort-Object Name -Descending |
                ForEach-Object { Join-Path $_.FullName "bin" } |
                Where-Object { Test-Path -LiteralPath (Join-Path $_ "g++.exe") -PathType Leaf } |
                Select-Object -First 1
            if ($candidate) {
                return $candidate
            }
        }
    }
    throw "A 64-bit MinGW compiler was not found on PATH or in the Qt Tools directory."
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

$qt = Find-QtInstallation -RequestedPrefix $QtPrefix -RequestedToolchain $Toolchain
$detectedQtPrefix = $qt.Prefix
$qtVersion = $qt.VersionText
$qtPluginDirectory = $qt.Plugins
$qtMajor = $qtVersion.Split('.')[0]
$qtBinDirectory = Join-Path $detectedQtPrefix "bin"
$qtRoot = Split-Path -Parent (Split-Path -Parent $detectedQtPrefix)
$cmake = Find-CMake -QtRoot $qtRoot

if ($qt.Kind -eq "MSVC") {
    $buildPreset = "vs2022-x64-$($Configuration.ToLowerInvariant())"
    $buildDirectory = Join-Path $repositoryRoot "build\vs2022-x64"
    $executable = Join-Path $buildDirectory "$Configuration\jydRenderer.exe"
    $ninja = $null
    $mingwBinDirectory = $null
} else {
    $buildPreset = $null
    $buildDirectory = Join-Path $repositoryRoot "build\package-mingw-x64"
    $executable = Join-Path $buildDirectory "jydRenderer.exe"
    $ninja = Find-Ninja -QtRoot $qtRoot
    $mingwBinDirectory = Find-MinGwBinDirectory -QtRoot $qtRoot
}

Write-Host "Qt $qtVersion selected:"
Write-Host "  Prefix:    $detectedQtPrefix"
Write-Host "  Toolchain: $($qt.Kind)"

$originalPath = $env:Path
$additionalToolPaths = @($qtBinDirectory)
if ($qt.Kind -eq "MinGW") {
    $additionalToolPaths += $mingwBinDirectory
    $additionalToolPaths += Split-Path -Parent $ninja
}
$env:Path = (($additionalToolPaths | Select-Object -Unique) -join ';') + ';' + $originalPath

Push-Location $repositoryRoot
try {
    if (-not $SkipBuild) {
        if ($qt.Kind -eq "MSVC") {
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
            Invoke-NativeCommand -FilePath $cmake -ArgumentList $configureArguments
            Invoke-NativeCommand -FilePath $cmake -ArgumentList @(
                "--build", "--preset", $buildPreset
            )
        } else {
            $configureArguments = @(
                "-S", $repositoryRoot,
                "-B", $buildDirectory,
                "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=$Configuration",
                "-DCMAKE_PREFIX_PATH=$detectedQtPrefix",
                "-DCMAKE_MAKE_PROGRAM=$ninja",
                "-DCMAKE_C_COMPILER=$(Join-Path $mingwBinDirectory 'gcc.exe')",
                "-DCMAKE_CXX_COMPILER=$(Join-Path $mingwBinDirectory 'g++.exe')"
            )
            Invoke-NativeCommand -FilePath $cmake -ArgumentList $configureArguments
            Invoke-NativeCommand -FilePath $cmake -ArgumentList @(
                "--build", $buildDirectory
            )
        }
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
        if ($qt.Kind -eq "MinGW") {
            throw "windeployqt failed and could not deploy the MinGW runtime."
        } else {
            Write-Warning "windeployqt failed; using the local DLL dependency fallback."
        }
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

    if ($qt.Kind -eq "MSVC") {
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
    }

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
    $env:Path = $originalPath
    Pop-Location
}
