[CmdletBinding()]
param(
  [string]$Bind = "0.0.0.0",
  [int]$Port = 18181,
  [string]$ConfigurePreset = "ninja-debug",
  [string]$BuildPreset = "build-debug",
  [string]$TestPreset = "test-debug",
  [string]$DbPath = "",
  [string]$WebRoot = "",
  [string]$LogFile = "",
  [switch]$RunTests,
  [switch]$NoOpen,
  [int]$ReadyTimeoutSec = 30
)

$ErrorActionPreference = "Stop"
# In PowerShell 7+, keep native stderr as output so we can show full CMake diagnostics.
if (Get-Variable -Name PSNativeCommandUseErrorActionPreference -ErrorAction SilentlyContinue) {
  $PSNativeCommandUseErrorActionPreference = $false
}

function Write-Step {
  param([string]$Message)
  Write-Host "==> $Message" -ForegroundColor Cyan
}

function Command-Exists {
  param([string]$Name)
  return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Normalize-PathForCMake {
  param([string]$PathValue)
  if ([string]::IsNullOrWhiteSpace($PathValue)) {
    return ""
  }
  return ($PathValue -replace "\\", "/")
}

function Looks-Like-VsBundledVcpkgRoot {
  param([string]$PathValue)
  if ([string]::IsNullOrWhiteSpace($PathValue)) {
    return $false
  }
  return ($PathValue -match "Microsoft Visual Studio[\\/].+[\\/]VC[\\/]vcpkg")
}

function Invoke-NativeLogged {
  param(
    [Parameter(Mandatory = $true)][string]$Command,
    [string[]]$Arguments = @()
  )

  $previousErrorAction = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  $capturedLines = New-Object System.Collections.Generic.List[object]
  try {
    & $Command @Arguments 2>&1 | ForEach-Object {
      $capturedLines.Add($_)
      if ($_ -is [System.Management.Automation.ErrorRecord]) {
        Write-Host ($_.ToString()) -ForegroundColor Red
      } else {
        Write-Host "$_"
      }
    }
    $exitCode = $LASTEXITCODE
  } finally {
    $ErrorActionPreference = $previousErrorAction
  }

  return @{
    ExitCode = $exitCode
    Lines = @($capturedLines.ToArray())
  }
}

function Join-Lines {
  param([object[]]$Lines)
  if ($null -eq $Lines) {
    return ""
  }
  return ($Lines | ForEach-Object { "$_" }) -join "`n"
}

function Show-ConfigureHints {
  param(
    [object[]]$Lines,
    [string]$BuildDir = ""
  )

  $blob = Join-Lines -Lines $Lines
  if (-not $blob) {
    return
  }

  Write-Host ""
  Write-Host "Common fixes for configure failures:" -ForegroundColor Yellow

  if ($blob -match "Could NOT find SQLite3|Could not find SQLite3|SQLite3_FOUND|SQLite3_LIBRARY|SQLite3_INCLUDE_DIR") {
    Write-Host "- SQLite3 development files are missing." -ForegroundColor Yellow
    Write-Host "  Recommended on Windows (vcpkg):" -ForegroundColor Yellow
    Write-Host "    1) git clone https://github.com/microsoft/vcpkg C:\vcpkg" -ForegroundColor Yellow
    Write-Host "    2) C:\vcpkg\bootstrap-vcpkg.bat" -ForegroundColor Yellow
    Write-Host "    3) C:\vcpkg\vcpkg.exe install sqlite3:x64-windows" -ForegroundColor Yellow
    Write-Host "    4) setx CMAKE_TOOLCHAIN_FILE C:\vcpkg\scripts\buildsystems\vcpkg.cmake" -ForegroundColor Yellow
    Write-Host "  Then open a new Developer PowerShell and re-run this launcher." -ForegroundColor Yellow
  }

  if ($blob -match "CMAKE_CXX_COMPILER|No CMAKE_CXX_COMPILER|No CMAKE_C_COMPILER|is not a full path to an existing compiler") {
    Write-Host "- C++ compiler not found in this shell." -ForegroundColor Yellow
    Write-Host "  Install Visual Studio Build Tools / Community with Desktop development with C++." -ForegroundColor Yellow
    Write-Host "  Then run this script from Developer PowerShell for Visual Studio." -ForegroundColor Yellow
  }

  if ($blob -match "CMAKE_MAKE_PROGRAM|Ninja") {
    Write-Host "- Ninja may be missing from PATH." -ForegroundColor Yellow
    Write-Host "  Install: winget install Ninja-build.Ninja" -ForegroundColor Yellow
  }

  if ($blob -match "FindPackageHandleStandardArgs\.cmake") {
    Write-Host "- A required dependency was not found by CMake." -ForegroundColor Yellow
    Write-Host "  Scroll up to the first 'Could NOT find ...' line for the exact package name." -ForegroundColor Yellow
  }

  if (-not [string]::IsNullOrWhiteSpace($BuildDir)) {
    $errorLog = Join-Path $BuildDir "CMakeFiles\CMakeError.log"
    $outputLog = Join-Path $BuildDir "CMakeFiles\CMakeOutput.log"
    if (Test-Path $errorLog) {
      Write-Host "  CMake error log: $errorLog" -ForegroundColor Yellow
    }
    if (Test-Path $outputLog) {
      Write-Host "  CMake output log: $outputLog" -ForegroundColor Yellow
    }
  }
}

if ($Port -lt 1 -or $Port -gt 65535) {
  throw "Invalid -Port value '$Port'. Expected 1..65535."
}

if ($ReadyTimeoutSec -lt 5) {
  throw "Invalid -ReadyTimeoutSec value '$ReadyTimeoutSec'. Expected >= 5."
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = (Resolve-Path (Join-Path $scriptDir "..")).Path

if ([string]::IsNullOrWhiteSpace($DbPath)) {
  $DbPath = Join-Path $repoRoot "data\tuxdmx.sqlite"
}
if ([string]::IsNullOrWhiteSpace($WebRoot)) {
  $WebRoot = Join-Path $repoRoot "web"
}
if ([string]::IsNullOrWhiteSpace($LogFile)) {
  $LogFile = Join-Path $repoRoot "data\tuxdmx.log"
}

$missing = New-Object System.Collections.Generic.List[string]
$optionalMissing = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]

if (-not (Command-Exists "cmake")) {
  $missing.Add("CMake 3.28+ (install: winget install Kitware.CMake)")
} else {
  $cmakeFirstLine = (& cmake --version | Select-Object -First 1)
  if ($cmakeFirstLine -match "cmake version ([0-9]+\.[0-9]+\.[0-9]+)") {
    $foundVersion = [version]$Matches[1]
    $requiredVersion = [version]"3.28.0"
    if ($foundVersion -lt $requiredVersion) {
      $missing.Add("CMake 3.28+ (found $foundVersion, install update: winget install Kitware.CMake)")
    }
  }
}

if ($ConfigurePreset -match "ninja" -and -not (Command-Exists "ninja")) {
  $missing.Add("Ninja build tool (install: winget install Ninja-build.Ninja)")
}

$hasCompiler = (Command-Exists "cl.exe") -or (Command-Exists "g++.exe") -or (Command-Exists "clang++.exe")
if (-not $hasCompiler) {
  $missing.Add("C++ compiler (install VS Community/Build Tools with Desktop development with C++)")
}

if (Command-Exists "cl.exe") {
  $clCommand = Get-Command "cl.exe" -ErrorAction SilentlyContinue
  $clPath = ""
  if ($clCommand -and $clCommand.Source) {
    $clPath = $clCommand.Source
  }
  if ($clPath -and ($clPath -notmatch "Hostx64[\\/]+x64")) {
    $missing.Add("x64 MSVC shell (open 'x64 Native Tools Command Prompt for VS', run 'powershell', then this script)")
  }
}

# Try to infer vcpkg root and toolchain automatically.
$vcpkgRoot = $env:VCPKG_ROOT
if (Looks-Like-VsBundledVcpkgRoot -PathValue $vcpkgRoot) {
  # Prefer standalone vcpkg over VS bundled vcpkg for consistent package installs.
  $warnings.Add("Ignoring VS bundled VCPKG_ROOT=$vcpkgRoot")
  $vcpkgRoot = ""
}

if ([string]::IsNullOrWhiteSpace($vcpkgRoot)) {
  if (Test-Path "C:\vcpkg\vcpkg.exe") {
    $vcpkgRoot = "C:\vcpkg"
  } elseif (Command-Exists "vcpkg.exe") {
    $vcpkgCmd = Get-Command "vcpkg.exe" -ErrorAction SilentlyContinue
    if ($vcpkgCmd -and $vcpkgCmd.Source) {
      $vcpkgRoot = Split-Path -Parent $vcpkgCmd.Source
    }
  }
  if (-not [string]::IsNullOrWhiteSpace($vcpkgRoot)) {
    $env:VCPKG_ROOT = $vcpkgRoot
    $warnings.Add("Auto-detected VCPKG_ROOT=$vcpkgRoot")
  }
}

if (-not [string]::IsNullOrWhiteSpace($vcpkgRoot)) {
  $vcpkgRootResolved = (Resolve-Path $vcpkgRoot -ErrorAction SilentlyContinue)
  if ($vcpkgRootResolved) {
    $vcpkgRoot = $vcpkgRootResolved.Path
    $env:VCPKG_ROOT = $vcpkgRoot
  }
  if ($env:Path -notlike "*$vcpkgRoot*") {
    $env:Path = "$vcpkgRoot;$env:Path"
    $warnings.Add("Temporarily added vcpkg to PATH for this run")
  }
}

if ([string]::IsNullOrWhiteSpace($env:VCPKG_TARGET_TRIPLET)) {
  $env:VCPKG_TARGET_TRIPLET = "x64-windows"
}

if ([string]::IsNullOrWhiteSpace($env:CMAKE_TOOLCHAIN_FILE) -and -not [string]::IsNullOrWhiteSpace($vcpkgRoot)) {
  $candidateToolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
  if (Test-Path $candidateToolchain) {
    $env:CMAKE_TOOLCHAIN_FILE = $candidateToolchain
    $warnings.Add("Auto-detected CMAKE_TOOLCHAIN_FILE=$candidateToolchain")
  }
}

if ([string]::IsNullOrWhiteSpace($vcpkgRoot) -or -not (Test-Path (Join-Path $vcpkgRoot "vcpkg.exe"))) {
  $missing.Add("vcpkg (install at C:\\vcpkg or set VCPKG_ROOT to your vcpkg folder)")
} else {
  $triplet = $env:VCPKG_TARGET_TRIPLET
  $sqliteHeader = Join-Path $vcpkgRoot "installed\$triplet\include\sqlite3.h"
  $sqliteLib = Join-Path $vcpkgRoot "installed\$triplet\lib\sqlite3.lib"
  if (-not (Test-Path $sqliteHeader) -or -not (Test-Path $sqliteLib)) {
    $missing.Add("sqlite3 for $triplet (run: $vcpkgRoot\\vcpkg.exe install sqlite3:$triplet)")
  }

  $portAudioHeader = Join-Path $vcpkgRoot "installed\$triplet\include\portaudio.h"
  $portAudioLib1 = Join-Path $vcpkgRoot "installed\$triplet\lib\portaudio.lib"
  $portAudioLib2 = Join-Path $vcpkgRoot "installed\$triplet\lib\portaudio_x64.lib"
  if (-not (Test-Path $portAudioHeader) -or (-not (Test-Path $portAudioLib1) -and -not (Test-Path $portAudioLib2))) {
    $optionalMissing.Add("PortAudio for real microphone input (install: $vcpkgRoot\\vcpkg.exe install portaudio:$triplet)")
  }

  $rtMidiHeader = Join-Path $vcpkgRoot "installed\$triplet\include\RtMidi.h"
  $rtMidiHeaderAlt = Join-Path $vcpkgRoot "installed\$triplet\include\rtmidi\RtMidi.h"
  $rtMidiLib = Join-Path $vcpkgRoot "installed\$triplet\lib\rtmidi.lib"
  if ((-not (Test-Path $rtMidiHeader) -and -not (Test-Path $rtMidiHeaderAlt)) -or -not (Test-Path $rtMidiLib)) {
    $optionalMissing.Add("RtMidi for server MIDI support (install: $vcpkgRoot\vcpkg.exe install rtmidi:$triplet)")
  }
}

if ([string]::IsNullOrWhiteSpace($env:CMAKE_TOOLCHAIN_FILE) -or -not (Test-Path $env:CMAKE_TOOLCHAIN_FILE)) {
  $missing.Add("CMAKE_TOOLCHAIN_FILE set to vcpkg toolchain (e.g. C:\\vcpkg\\scripts\\buildsystems\\vcpkg.cmake)")
}

if ($missing.Count -gt 0) {
  Write-Host ""
  Write-Host "Missing required tools:" -ForegroundColor Red
  foreach ($m in $missing) {
    Write-Host " - $m" -ForegroundColor Red
  }
  Write-Host ""
  Write-Host "Important: PATH changes from installers are not visible in this already-open terminal." -ForegroundColor Yellow
  Write-Host "If you just installed CMake/Ninja/Build Tools, close this PowerShell window and open a new one." -ForegroundColor Yellow
  Write-Host "Use 'x64 Native Tools Command Prompt for VS', then run 'powershell', then this launcher." -ForegroundColor Yellow
  Write-Host "If vcpkg/sqlite3 is missing, run:" -ForegroundColor Yellow
  Write-Host "  git clone https://github.com/microsoft/vcpkg C:\vcpkg" -ForegroundColor Yellow
  Write-Host "  C:\vcpkg\bootstrap-vcpkg.bat" -ForegroundColor Yellow
  Write-Host "  C:\vcpkg\vcpkg.exe install sqlite3:x64-windows" -ForegroundColor Yellow
  Write-Host "Then run this script again." -ForegroundColor Yellow
  exit 1
}

foreach ($w in $warnings) {
  Write-Host "[info] $w" -ForegroundColor DarkYellow
}

if ($optionalMissing.Count -gt 0) {
  Write-Host "[warn] Optional backends missing. Build will run, but some features will be unavailable:" -ForegroundColor Yellow
  foreach ($m in $optionalMissing) {
    Write-Host "  - $m" -ForegroundColor Yellow
  }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DbPath) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogFile) | Out-Null

Push-Location $repoRoot

$serverProcess = $null
$launched = $false

try {
  Write-Step "Configuring ($ConfigurePreset)"
  $configureArgs = @("--preset", $ConfigurePreset)
  if (-not [string]::IsNullOrWhiteSpace($env:CMAKE_TOOLCHAIN_FILE)) {
    $toolchainPath = Normalize-PathForCMake -PathValue $env:CMAKE_TOOLCHAIN_FILE
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$toolchainPath"
  }
  if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_TARGET_TRIPLET)) {
    $configureArgs += "-DVCPKG_TARGET_TRIPLET=$($env:VCPKG_TARGET_TRIPLET)"
  }
  $configureResult = Invoke-NativeLogged -Command "cmake" -Arguments $configureArgs
  if ($configureResult.ExitCode -ne 0) {
    $configureBlob = Join-Lines -Lines $configureResult.Lines
    $presetBuildDir = ""
    if ($configureBlob -match "Build files have been written to:\s*(.+)") {
      $presetBuildDir = $Matches[1].Trim()
    } elseif ($ConfigurePreset -eq "ninja-debug") {
      $presetBuildDir = Join-Path $repoRoot "build\debug"
    } elseif ($ConfigurePreset -eq "ninja-release") {
      $presetBuildDir = Join-Path $repoRoot "build\release"
    }
    Show-ConfigureHints -Lines $configureResult.Lines -BuildDir $presetBuildDir
    throw "CMake configure failed."
  }

  Write-Step "Building ($BuildPreset)"
  $buildResult = Invoke-NativeLogged -Command "cmake" -Arguments @("--build", "--preset", $BuildPreset)
  if ($buildResult.ExitCode -ne 0) {
    throw "CMake build failed."
  }

  if ($RunTests.IsPresent) {
    Write-Step "Running tests ($TestPreset)"
    $testResult = Invoke-NativeLogged -Command "ctest" -Arguments @("--preset", $TestPreset)
    if ($testResult.ExitCode -ne 0) {
      throw "CTest failed."
    }
  }

  $binaryCandidates = @(
    (Join-Path $repoRoot "build\debug\tuxdmx.exe"),
    (Join-Path $repoRoot "build\release\tuxdmx.exe")
  )

  $binaryPath = $null
  foreach ($candidate in $binaryCandidates) {
    if (Test-Path $candidate) {
      $binaryPath = $candidate
      break
    }
  }

  if (-not $binaryPath) {
    throw "tuxdmx.exe not found after build."
  }

  Write-Step "Starting server"
  $serverArgs = @(
    "--bind", $Bind,
    "--port", "$Port",
    "--db", $DbPath,
    "--web-root", $WebRoot,
    "--log-file", $LogFile
  )

  $serverProcess = Start-Process -FilePath $binaryPath -ArgumentList $serverArgs -PassThru -NoNewWindow
  $launched = $true

  $localUrl = "http://127.0.0.1:$Port"
  $readyUrl = "$localUrl/api/state"

  Write-Step "Waiting for server readiness ($readyUrl)"
  $ready = $false
  $deadline = (Get-Date).AddSeconds($ReadyTimeoutSec)
  while ((Get-Date) -lt $deadline) {
    $serverProcess.Refresh()
    if ($serverProcess.HasExited) {
      throw "Server exited before becoming ready. Exit code: $($serverProcess.ExitCode)"
    }

    try {
      $response = Invoke-RestMethod -Uri $readyUrl -Method Get -TimeoutSec 2
      if ($response.ok -eq $true) {
        $ready = $true
        break
      }
    } catch {
      # Keep retrying until timeout.
    }

    Start-Sleep -Milliseconds 250
  }

  if (-not $ready) {
    throw "Server did not become ready within $ReadyTimeoutSec seconds."
  }

  $lanIp = $null
  try {
    $lanIp = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop |
      Where-Object {
        $_.IPAddress -notlike "127.*" -and
        $_.IPAddress -notlike "169.254.*" -and
        $_.IPAddress -notlike "0.*"
      } |
      Select-Object -First 1 -ExpandProperty IPAddress
  } catch {
    $lanIp = $null
  }

  Write-Host ""
  Write-Host "TuxDMX is running" -ForegroundColor Green
  Write-Host "Local: $localUrl" -ForegroundColor Green
  if ($lanIp) {
    Write-Host "LAN:   http://$lanIp`:$Port" -ForegroundColor Green
  }
  Write-Host "Log:   $LogFile" -ForegroundColor Green
  Write-Host "Press Ctrl+C to stop."

  if (-not $NoOpen.IsPresent) {
    Start-Process $localUrl | Out-Null
  }

  Wait-Process -Id $serverProcess.Id
  $serverProcess.Refresh()
  if ($serverProcess.ExitCode -ne 0) {
    throw "Server exited with code $($serverProcess.ExitCode)."
  }
} finally {
  if ($launched -and $null -ne $serverProcess) {
    try {
      $serverProcess.Refresh()
      if (-not $serverProcess.HasExited) {
        Write-Step "Stopping server"
        Stop-Process -Id $serverProcess.Id -ErrorAction SilentlyContinue
        Start-Sleep -Milliseconds 200
        $serverProcess.Refresh()
        if (-not $serverProcess.HasExited) {
          Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue
        }
      }
    } catch {
      # Ignore cleanup failures.
    }
  }
  Pop-Location
}
