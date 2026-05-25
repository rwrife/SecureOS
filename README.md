# SecureOS

SecureOS is an experimental operating system focused on **zero-trust by default** behavior. Every process runs in total isolation and requires explicit user consent to access any resource.

## Quick Start

```bash
# Clone and run — that's it
git clone https://github.com/rwrife/SecureOS.git
cd SecureOS

# macOS / Linux
./start.sh

# Windows (PowerShell)
.\start.ps1
```

The start script automatically:
1. Checks for Docker and QEMU (installs them if missing, with your confirmation)
2. Builds the OS inside a Docker container
3. Boots SecureOS in QEMU

### Options

| Flag | Description |
|------|-------------|
| `--graphics` / `-Graphics` | Boot with VGA display window |
| `--build-only` / `-BuildOnly` | Build without booting |
| `--setup-only` / `-SetupOnly` | Install dependencies only |
| `--clean` / `-Clean` | Remove artifacts before building |
| `--skip-setup` / `-SkipSetup` | Skip dependency checks |

### Individual Scripts

For iterative development, use the scripts separately:

```bash
./scripts/build.sh [kernel|disk|all]    # Build (default: all)
./scripts/boot.sh [console|graphics]    # Boot (default: console)
./scripts/test.sh [test_name|--all]     # Run tests
```

Windows:
```powershell
.\scripts\build.ps1 [kernel|disk|all]
.\scripts\boot.ps1 [console|graphics]
.\scripts\test.ps1 [test_name|--all]
```

## Prerequisites

Only two tools are needed on your host machine:
- **Docker** — all compilation happens inside a container
- **QEMU** — runs the OS with hardware emulation

The setup scripts handle installation:
- macOS: `./scripts/setup-macos.sh`
- Linux: `./scripts/setup-linux.sh`
- Windows: `.\scripts\setup-windows.ps1`

## Demo

After booting (via `./start.sh` or `./scripts/boot.sh`), interact with the OS:

```text
secureos> help
secureos> apps
secureos> run filedemo
secureos> cat appdemo.txt
secureos> exit pass
```

For graphics mode with the VGA display:
```bash
./start.sh --graphics
```

## Architecture

- `kernel/` — Minimal kernel: process isolation, capability system, hardware abstraction
- `user/` — User-space libraries, apps, and OS commands
- `build/` — Dockerfile, internal build scripts, QEMU configs
- `scripts/` — Host-side entry points (setup, build, boot, test)
- `manifests/` — Capability manifests for processes
- `docs/` — Architecture decisions, ABI reference, test plans
  - `docs/abi/` — **canonical ABI reference** (`OS_ABI_VERSION = 0`):
    syscall surface, IPC wire format, capability handle representation,
    launcher manifest schema, and the `OS_ABI_VERSION` policy. See
    [`docs/abi/README.md`](docs/abi/README.md) for the full index.
- `plans/` — Planning documents for future work

## Design Principles

- **Capability-native**: All resource access goes through explicit capability gates
- **Deny-by-default**: Processes start with zero permissions
- **User consent**: Hardware/resource access requires interactive confirmation
- **Deterministic builds**: Pinned Docker toolchain ensures reproducibility
- **Multi-architecture ready**: HAL layer abstracts hardware (x86 target first)

## Contributing

See `CONTRIBUTING.md` for full contributor guidance.

