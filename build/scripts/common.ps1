Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-SecureOSRootDir {
  param(
    [Parameter(Mandatory = $true)]
    [string]$ScriptRoot
  )

  return (Resolve-Path (Join-Path $ScriptRoot "..\..")).Path
}

function Get-ToolchainImage {
  if ($env:SECUREOS_TOOLCHAIN_IMAGE) {
    return $env:SECUREOS_TOOLCHAIN_IMAGE
  }

  return "secureos/toolchain:bookworm-2026-02-12"
}

function Get-ToolchainDockerfile {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RootDir
  )

  if ($env:SECUREOS_TOOLCHAIN_DOCKERFILE) {
    return $env:SECUREOS_TOOLCHAIN_DOCKERFILE
  }

  return (Join-Path $RootDir "build/docker/Dockerfile.toolchain")
}

function Assert-DockerAvailable {
  if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
    throw "docker is required"
  }

  $cmdExe = if ($env:ComSpec) { $env:ComSpec } else { "cmd.exe" }
  & $cmdExe /d /c "docker info >nul 2>nul"

  if ($LASTEXITCODE -ne 0) {
    throw "docker is installed but the daemon is unavailable. Start Docker Desktop and retry."
  }
}

function Ensure-ToolchainImage {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RootDir,

    [Parameter(Mandatory = $true)]
    [string]$ImageTag,

    [Parameter(Mandatory = $true)]
    [string]$Dockerfile
  )

  docker image inspect $ImageTag *> $null
  if ($LASTEXITCODE -eq 0) {
    docker run --rm $ImageTag bash -lc "command -v grub-mkrescue >/dev/null 2>&1; test -d /usr/lib/grub/i386-pc" *> $null
    if ($LASTEXITCODE -eq 0) {
      return
    }

    Write-Host "Toolchain image is missing GRUB BIOS boot assets; rebuilding: $ImageTag"
  }

  Write-Host "Toolchain image not found: $ImageTag"
  Write-Host "Building it from: $Dockerfile"
  docker build -f $Dockerfile -t $ImageTag $RootDir
  if ($LASTEXITCODE -ne 0) {
    throw "Failed to build toolchain image: $ImageTag"
  }
}

function Invoke-ToolchainScript {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RootDir,

    [Parameter(Mandatory = $true)]
    [string]$ImageTag,

    [Parameter(Mandatory = $true)]
    [string]$ScriptText
  )

  # Normalize line endings so bash does not see CRLF artifacts from Windows.
  $normalizedScript = $ScriptText -replace "`r`n", "`n" -replace "`r", "`n"

  docker run --rm -v "${RootDir}:/workspace" -w /workspace $ImageTag bash -lc $normalizedScript
  if ($LASTEXITCODE -ne 0) {
    throw "Toolchain command failed with exit code $LASTEXITCODE"
  }
}

function Stop-SecureOSActiveInstances {
  param(
    [Parameter(Mandatory = $true)]
    [string]$RootDir,

    [Parameter(Mandatory = $true)]
    [string]$ImageTag
  )

  if (Get-Command docker -ErrorAction SilentlyContinue) {
    $containerIds = @()
    $dockerPs = docker ps --filter "ancestor=$ImageTag" --format "{{.ID}}"
    if ($LASTEXITCODE -eq 0 -and $dockerPs) {
      $containerIds = @($dockerPs | Where-Object { $_ -and $_.Trim().Length -gt 0 })
    }

    if ($containerIds.Count -gt 0) {
      Write-Host "Stopping existing SecureOS toolchain containers..."
      docker stop $containerIds *> $null
    }
  }

  $normalizedRoot = $RootDir.Replace('\\', '/').ToLowerInvariant()
  $qemuProcs = Get-CimInstance Win32_Process -Filter "Name='qemu-system-x86_64.exe'" -ErrorAction SilentlyContinue
  foreach ($proc in $qemuProcs) {
    $cmd = [string]$proc.CommandLine
    if (-not $cmd) {
      continue
    }

    $cmdNorm = $cmd.Replace('\\', '/').ToLowerInvariant()
    if ($cmdNorm.Contains($normalizedRoot) -or $cmdNorm.Contains("secureos-disk.img") -or $cmdNorm.Contains("secureos.iso")) {
      try {
        Stop-Process -Id $proc.ProcessId -Force -ErrorAction Stop
      } catch {
      }
    }
  }
}
