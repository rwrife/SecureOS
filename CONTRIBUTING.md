# Contributing to SecureOS

Thanks for contributing to SecureOS.

This guide covers:
- Local setup requirements
- How to build the project
- How to run the SecureOS console in headless and graphical modes

## Prerequisites

Required for all contributors:
- Docker
- Git

Required for graphical mode only:
- Host QEMU binary on PATH: `qemu-system-x86_64` (or `qemu-system-x86_64.exe` on Windows)

Recommended:
- At least 4 GB free disk space for images and artifacts

## Clone and Enter the Repo

```bash
git clone https://github.com/rwrife/SecureOS.git
cd SecureOS
```

## Setup

### Windows

No extra bootstrap script is required.

Verify Docker is available:

```powershell
docker --version
```

### macOS

Run the bootstrap helper:

```bash
./scripts/setup-macos.sh
```

Then verify Docker:

```bash
docker --version
```

### Linux

Install Docker using your distro package manager and verify:

```bash
docker --version
```

## Build

The build wrappers automatically ensure the pinned toolchain image is available.

### Build everything for testing

Windows PowerShell:

```powershell
.\build\scripts\build.ps1 build-all
```

macOS/Linux:

```bash
./build/scripts/build.sh build-all
```

### Common build targets

Windows PowerShell:

```powershell
.\build\scripts\build.ps1 kernel
.\build\scripts\build.ps1 image
.\build\scripts\build.ps1 disk
```

macOS/Linux:

```bash
./build/scripts/build.sh kernel
./build/scripts/build.sh image
./build/scripts/build.sh disk
```

## Run the Console

### Headless console (no graphics)

This launches an interactive SecureOS prompt using the containerized QEMU path.

Windows PowerShell:

```powershell
.\build\scripts\build.ps1 console
```

macOS/Linux:

```bash
./build/scripts/build.sh console
```

Notes:
- Type commands at the `secureos>` prompt.
- Use `exit pass` to stop QEMU cleanly.

### Graphical console

This launches QEMU with a graphical window and serial console input.

Windows PowerShell:

```powershell
.\build\scripts\build.ps1 graphics
```

macOS/Linux:

```bash
./build/scripts/build.sh graphics
```

Notes:
- Keep typing commands in the terminal serial console.
- The QEMU graphics window is for display output.
- If graphics mode fails, confirm `qemu-system-x86_64` is installed and available on PATH.

## Demo Applications

After launching the console (`secureos>` prompt), you can explore and run demo apps.

### List available apps

At the prompt:

```text
apps
```

This prints the currently discoverable packaged apps on disk.

### Run the filedemo app

At the prompt:

```text
run filedemo
cat appdemo.txt
```

What this demonstrates:
- Launching a packaged user app through the process runtime
- Writing data to the filesystem from user space
- Reading the generated file back through the shell

### Networking demos

SecureOS also includes networking-focused demos/commands (for example `ifconfig`, `ping`, and `http`).
Use `help` in the console to see available command usage and then execute them directly at `secureos>`.

### Exiting the demo session

At the prompt:

```text
exit pass
```

This exits QEMU cleanly and returns control to your host terminal.

## Validate Artifacts

After successful builds, key outputs include:
- `artifacts/kernel/secureos.iso`
- `artifacts/disk/secureos-disk.img`
- `artifacts/os/*.bin`
- `artifacts/user/**/*.bin`
- `artifacts/lib/*.lib`

## Coding and Planning Expectations

Before opening a PR, review:
- `AGENTS.md`
- `docs/CODING_CONVENTIONS.md`

Project-specific expectations include:
- Keep PowerShell and shell build scripts in sync
- Add plan documents under `docs/plans` for major implementation work
- Keep hardware access behind HAL abstractions

## Pull Request Checklist

- Build succeeds locally (`build-all`)
- Relevant tests pass for your change
- New commands include help resources where applicable
- Documentation is updated for behavior changes
