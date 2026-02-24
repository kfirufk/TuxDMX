[CmdletBinding()]
param(
  [string]$Bind = "0.0.0.0",
  [int]$Port = 8080,
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

function Write-Step {
  param([string]$Message)
  Write-Host "==> $Message" -ForegroundColor Cyan
}

function Command-Exists {
  param([string]$Name)
  return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Join-Lines {
  param([object[]]$Lines)
  if ($null -eq $Lines) {
    return ""
  }
  return ($Lines | ForEach-Object { "$_" }) -join "`n"
}

function Show-ConfigureHints {
  param([object[]]$Lines)

  $blob = Join-Lines -Lines $Lines
  if (-not $blob) {
    return
  }

  Write-Host ""
  Write-Host "Common fixes for configure failures:" -ForegroundColor Yellow

  if ($blob -match "Could NOT find SQLite3|Could not find SQLite3|SQLite3_FOUND") {
    Write-Host "- SQLite3 development files are missing." -ForegroundColor Yellow
    Write-Host "  Download/install one of:" -ForegroundColor Yellow
    Write-Host "    - vcpkg + sqlite3 package: https://github.com/microsoft/vcpkg" -ForegroundColor Yellow
    Write-Host "    - SQLite prebuilt development package for Windows." -ForegroundColor Yellow
  }

  if ($blob -match "CMAKE_CXX_COMPILER|No CMAKE_CXX_COMPILER|No CMAKE_C_COMPILER|is not a full path to an existing compiler") {
    Write-Host "- C++ compiler not found in this shell." -ForegroundColor Yellow
    Write-Host "  Install Visual Studio 2022 Build Tools (Desktop development with C++)." -ForegroundColor Yellow
    Write-Host "  Then run this script from Developer PowerShell for VS 2022." -ForegroundColor Yellow
  }

  if ($blob -match "CMAKE_MAKE_PROGRAM|Ninja") {
    Write-Host "- Ninja may be missing from PATH." -ForegroundColor Yellow
    Write-Host "  Install: winget install Ninja-build.Ninja" -ForegroundColor Yellow
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
  $missing.Add("C++ compiler (recommended: Visual Studio 2022 Build Tools + Developer PowerShell)")
}

if ($missing.Count -gt 0) {
  Write-Host ""
  Write-Host "Missing required tools:" -ForegroundColor Red
  foreach ($m in $missing) {
    Write-Host " - $m" -ForegroundColor Red
  }
  Write-Host ""
  Write-Host "After installation, reopen terminal and run this script again." -ForegroundColor Yellow
  exit 1
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DbPath) | Out-Null
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogFile) | Out-Null

Push-Location $repoRoot

$serverProcess = $null
$launched = $false

try {
  Write-Step "Configuring ($ConfigurePreset)"
  $configureOutput = & cmake --preset $ConfigurePreset 2>&1 | Tee-Object -Variable configureLines
  if ($LASTEXITCODE -ne 0) {
    Show-ConfigureHints -Lines $configureLines
    throw "CMake configure failed."
  }

  Write-Step "Building ($BuildPreset)"
  & cmake --build --preset $BuildPreset
  if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed."
  }

  if ($RunTests.IsPresent) {
    Write-Step "Running tests ($TestPreset)"
    & ctest --preset $TestPreset
    if ($LASTEXITCODE -ne 0) {
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
