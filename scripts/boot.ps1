# scripts/boot.ps1 - Host-side QEMU launcher (Windows)
#
# Launches QEMU on the host with the SecureOS ISO and disk image.
# QEMU runs on the host (not in Docker) for display/device access.
#
# Usage: scripts\boot.ps1 [console|graphics]  (default: console)

[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [string]$Mode = "console"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$IsoPath = Join-Path $RootDir "artifacts\kernel\secureos.iso"
$DiskPath = Join-Path $RootDir "artifacts\disk\secureos-disk.img"

# Verify artifacts exist
if (-not (Test-Path $IsoPath)) {
  Write-Host "ERROR: Kernel ISO not found at $IsoPath" -ForegroundColor Red
  Write-Host "Run .\scripts\build.ps1 first."
  exit 1
}

if (-not (Test-Path $DiskPath)) {
  Write-Host "ERROR: Disk image not found at $DiskPath" -ForegroundColor Red
  Write-Host "Run .\scripts\build.ps1 first."
  exit 1
}

# Find QEMU
$QemuExe = $null
if (Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue) {
  $QemuExe = "qemu-system-x86_64"
} elseif (Test-Path "C:\Program Files\qemu\qemu-system-x86_64.exe") {
  $QemuExe = "C:\Program Files\qemu\qemu-system-x86_64.exe"
} else {
  Write-Host "ERROR: qemu-system-x86_64 not found." -ForegroundColor Red
  Write-Host "Run .\scripts\setup-windows.ps1 to install QEMU."
  exit 1
}

# Build QEMU arguments
$QemuArgs = @(
  "-cdrom", $IsoPath,
  "-boot", "d",
  "-drive", "format=raw,file=$DiskPath,if=ide,index=0,media=disk",
  "-device", "isa-debug-exit,iobase=0xf4,iosize=0x04",
  "-serial", "stdio",
  "-monitor", "none",
  "-m", "256M",
  "-smp", "1",
  "-no-reboot",
  "-rtc", "base=utc,clock=host",
  "-netdev", "user,id=net0",
  "-device", "virtio-net-pci,disable-modern=on,netdev=net0"
)

switch ($Mode) {
  "console" {
    Write-Host "Booting SecureOS (serial console mode)..."
    Write-Host "Type commands at the secureos> prompt. Use 'exit pass' to stop QEMU."
    Write-Host ""
    $QemuArgs += "-nographic"
  }
  "graphics" {
    Write-Host "Booting SecureOS (graphics mode)..."
    Write-Host "A QEMU window will open with the VGA display."
    Write-Host "Serial I/O is still connected to this terminal."
    Write-Host ""
  }
  default {
    Write-Host "Unknown mode: $Mode" -ForegroundColor Red
    Write-Host "Usage: scripts\boot.ps1 [console|graphics]"
    exit 1
  }
}

# Launch QEMU
$process = Start-Process -FilePath $QemuExe -ArgumentList $QemuArgs -NoNewWindow -PassThru -Wait
$rc = $process.ExitCode

# Exit code 33 = debug exit with pass code
if ($rc -eq 33) {
  Write-Host ""
  Write-Host "SecureOS exited cleanly (PASS)." -ForegroundColor Green
  exit 0
}

exit $rc
