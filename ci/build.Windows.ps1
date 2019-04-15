
# An EXAMPLE Build Script for Windows
# Note that several details here may not be appropriate for your environment, so I recommend you adapt for your purposes.

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"


#### HELPER FUNCTIONS ####

function _log($msg) {
    Write-Output "build.Windows: ${msg}"
}

function _die($msg) {
    _log "FATAL: ${msg}"
    Exit 1
}

function _resetEnvironmentToSaved() {
    Remove-Item env:* -Recurse -Force
    ForEach($envPairKey in $envOriginal.KEYS.GetEnumerator()) {
        Set-Content "env:\$($envPairKey)" $envOriginal[$envPairKey]
    }
}

function _refreshEnvironmentComeHellOrHighWater() {
    _log "Trying really hard to refresh environment..."
    _resetEnvironmentToSaved
    # Use Choco and Visual Studio Build Tools to build a new environment.
    Start-Process -FilePath cmd.exe -ArgumentList ("/c", "C:\ProgramData\chocolatey\bin\RefreshEnv.cmd && C:\BuildTools\VC\Auxiliary\Build\vcvarsall.bat amd64 && set > $myTemp\vcvars.txt") -Wait -NoNewWindow 
    # Pull the results in and merge them.
    Get-Content "$myTemp\vcvars.txt" | Foreach-Object {
        if ($_ -match "^(.*?)=(.*)$") {
            Set-Content "env:\$($matches[1])" $matches[2]
        }
    }
}

function _checkError($acceptableExitCode = 0) {
    $myExitCode = $LastExitCode
    if($myExitCode -ne $acceptableExitCode) {
        _die "Expected ${acceptableExitCode}!"
    }
}

function _walkDeps($image, $outputPath) {
    _log "Walking dependencies for ${image}"
    $lines = $(dumpbin /dependents "${image}")
    _checkError
    $lines | ForEach-Object{
        if($_ -like "*.dll" -And $_ -notlike "*Dump of file*") {
            $blacklisted = $false
            $dll = $_.Trim()
            _log "Apparently ${image} requires ${dll}"
            $candidates = @()
            $env:Path.Split(';') | Foreach-Object {
                if($_.Length) {
                    $candidate = Join-Path $_ $dll
                    if(Test-Path $candidate) {
                        if($candidate -match '^C:\\Windows') {
                            _log "${dll} apparently is available in a blacklisted path ( $candidate ) so we will not package this dependency."
                            $Global:depsBlacklisted += $dll
                            $blacklisted = $true
                        }
                        $candidates += $candidate
                    }
                }
            }
            if(!$candidates.Length) {
                _die "Cannot find ${dll}! Is it in your PATH?"
            }
            if(!$blacklisted) {
                $dllHash = ""
                $dllHashSet = $false
                $bestCandidate = ""
                foreach($candidate in $candidates) {
                    # _log "Attempting to hash: ${candidate}"
                    $bestCandidate = $candidate
                    $myHash = $(Get-FileHash -Path "${candidate}").Hash
                    if($dllHashSet) {
                        if($dllHash -ne $myHash) {
                            _die "You have multiple differing copies of $dll in your PATH! This can lead to undefined behavior."
                        }
                    } else {
                        # _log "${dll} hash: ${myHash}"
                        $dllHash = $myHash
                        $dllHashSet = $true
                    }
                }
                _log "Copying: ${bestCandidate} -> ${outputPath}"
                Copy-Item -Path "${bestCandidate}" -Destination "${outputPath}" -Force
                _walkDeps $bestCandidate $outputPath
            }
        }
    }
}

#### ENTRY POINT ####

_log "=== Starting up... ==="

# Save the initial environment, because we'll need to be able to reset it.
$envOriginal = @{}
$envPairs = Get-ChildItem env:
foreach($envPair in $envPairs) {
    $envOriginal[$envPair.Name] = $envPair.Value;
}

_log "=== Configuring Build Environment... ==="
$myTemp = ${env:TEMP}
$myBase = "${PSScriptRoot}\.."
_log "BASE Folder is: ${myBase}"
_log "TEMP Folder is: ${myTemp}"
_refreshEnvironmentComeHellOrHighWater
# $env:PKG_CONFIG_PATH = "C:\installroot\lib\pkgconfig;C:\MSYS2\usr\lib\pkgconfig"
$env:PKG_CONFIG_PATH = "/c/installroot/lib/pkgconfig:/c/MSYS2/usr/lib/pkgconfig"
$env:PATH = "${env:PATH};C:\Program Files (x86)\Windows Kits\10\Redist\10.0.17763.0\ucrt\DLLs\x64\;C:\installroot\bin\;C:\installroot\lib;C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v10.1\bin\;C:\installroot\x64\vc15\bin\"

_log "=== CMake Configuring... ==="
Set-Location "${myBase}"
New-Item -ItemType directory -Path build -Force -ErrorAction SilentlyContinue
Remove-Item build\* -Recurse -Force -ErrorAction SilentlyContinue
Set-Location build
& 'C:\Program Files\CMake\bin\cmake.exe' -D "CMAKE_PREFIX_PATH=C:/installroot;C:/MSYS2/mingw64" -D "CMAKE_INSTALL_PREFIX=AppDir" -A x64 ..
_checkError
_log "=== CMake Building... ==="
& 'C:\Program Files\CMake\bin\cmake.exe' --build . --config Release --target install -- "-v:normal"
_checkError

_log "=== Walking Dependencies... ==="
_walkDeps ".\Release\yer-face.exe" "${myBase}\build\AppDir\"
_log "=== Walking Dependencies Finished... ==="

_log "=== Packaging ... ==="
$versionString = $(Get-Content ".\VersionString" -Raw).Trim()
Move-Item AppDir "${versionString}-x86_64"
Compress-Archive -Path "${versionString}-x86_64" -CompressionLevel Optimal -DestinationPath "${versionString}-x86_64.zip"
bash -c "sha256sum ${versionString}-x86_64.zip > ${versionString}.SHA256SUMS"
_checkError

_resetEnvironmentToSaved
_log "=== All done. ==="
Exit 0
