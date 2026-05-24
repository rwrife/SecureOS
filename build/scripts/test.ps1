[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [string]$TestName = "hello_boot",

  [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

function Show-Usage {
  Write-Host "Usage: test.ps1 [hello_boot|hello_boot_negative|harness_negative|cap_api_contract|capability_table|cap_table_skeleton|cap_handle_repr|cap_handle_revoke_subject|cap_handle_revoke_subtree|capability_gate|capability_audit|capability_audit_fixture|cap_broker|cap_deny_marker_shape|workflow_rule|event_bus|scheduler|sof_format|tls|https|bearssl_compile|fs_service|launcher_fs|app_runtime|ed25519|cert_chain|codesign|kernel_console|kernel_filedemo|kernel_persistence|kernel_sessions|ipc_sync_v0|ipc_port_lifecycle|ipc_handle_gate|ipc_bounds|m1_ipc_demo|process_table|proc_sched|syscall_entry_stub|validate_capability_registry|capability_registry_drift|parity|canary_must_fail]"
  Write-Host ""
  Write-Host "Runs SecureOS test targets inside the pinned toolchain container."
}

if ($Help) {
  Show-Usage
  exit 0
}

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

# 'parity' is a pure host-side metadata check (no toolchain image needed).
if ($TestName -eq 'parity') {
  & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'test_shell_parity.ps1')
  exit $LASTEXITCODE
}

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile
Stop-SecureOSActiveInstances -RootDir $rootDir -ImageTag $imageTag

$testScript = switch ($TestName) {
  "hello_boot" { "./build/scripts/test_boot_sector.sh; ./build/scripts/run_qemu.sh --test hello_boot" }
  "hello_boot_negative" { "./build/scripts/test_boot_sector_fail.sh; ./build/scripts/run_qemu.sh --test hello_boot_fail" }
  "harness_negative" { "./build/scripts/test_harness_negative.sh" }
  "cap_api_contract" { "./build/scripts/test_cap_api_contract.sh" }
  "capability_table" { "./build/scripts/test_capability_table.sh" }
  "cap_table_skeleton" { "./build/scripts/test_cap_table_skeleton.sh" }
  "cap_handle_repr" { "./build/scripts/test_cap_handle_repr.sh" }
  "cap_handle_revoke_subject" { "./build/scripts/test_cap_handle_revoke_subject.sh" }
  "cap_handle_revoke_subtree" { "./build/scripts/test_cap_handle_revoke_subtree.sh" }
  "capability_gate" { "./build/scripts/test_capability_gate.sh" }
  "capability_audit" { "./build/scripts/test_capability_audit.sh" }
  "capability_audit_fixture" { "./build/scripts/test_capability_audit_fixture.sh" }
  "cap_deny_marker_shape" { "./build/scripts/test_cap_deny_marker_shape.sh" }
  "event_bus" { "./build/scripts/test_event_bus.sh" }
  "scheduler" { "./build/scripts/test_scheduler.sh" }
  "sof_format" { "./build/scripts/test_sof_format.sh" }
  "fs_service" { "./build/scripts/test_fs_service.sh" }
  "launcher_fs" { "./build/scripts/test_launcher_fs.sh" }
  "app_runtime" { "./build/scripts/test_app_runtime.sh" }
  "kernel_console" { "./build/scripts/build_kernel_image.sh; ./build/scripts/build_disk_image.sh; ./build/scripts/run_qemu.sh --test kernel_console" }
  "kernel_filedemo" { "./build/scripts/build_kernel_image.sh; ./build/scripts/build_disk_image.sh; ./build/scripts/run_qemu.sh --test kernel_filedemo" }
  "kernel_persistence" { "./build/scripts/test_kernel_persistence.sh" }
  "ed25519" { "./build/scripts/test_ed25519.sh" }
  "cert_chain" { "./build/scripts/test_cert_chain.sh" }
  "codesign" { "./build/scripts/test_codesign.sh" }
  "tls" { "./build/scripts/test_tls.sh" }
  "https" { "./build/scripts/test_https.sh" }
  "bearssl_compile" { "./build/scripts/test_bearssl_compile.sh" }
  "kernel_sessions" { "./build/scripts/build_kernel_image.sh; ./build/scripts/build_disk_image.sh; ./build/scripts/run_qemu.sh --test kernel_sessions" }
  "ipc_sync_v0" { "./build/scripts/test_ipc_sync_v0.sh" }
  "ipc_port_lifecycle" { "./build/scripts/test_ipc_port_lifecycle.sh" }
  "ipc_handle_gate" { "./build/scripts/test_ipc_handle_gate.sh" }
  "ipc_bounds" { "./build/scripts/test_ipc_bounds.sh" }
  "process_table" { "./build/scripts/test_process_table.sh" }
  "proc_sched" { "./build/scripts/test_proc_sched.sh" }
  "m1_ipc_demo" { "./build/scripts/test_m1_ipc_demo.sh" }
  "syscall_entry_stub" { "./build/scripts/test_syscall_entry_stub.sh" }
  "validate_capability_registry" { "./build/scripts/validate_capability_registry.sh" }
  "capability_registry_drift" { "./tests/harness/capability_registry_drift_test.sh" }
  "harness_defense" { "./build/scripts/test_harness_defense.sh" }
  "canary_must_fail" { "./tests/integration/_canary_must_fail/canary_must_fail.sh" }
  "workflow_rule" { "./build/scripts/test_workflow_rule.sh" }
  default {
    Write-Host "Unknown test: $TestName"
    Show-Usage
    exit 1
  }
}

Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText $testScript
