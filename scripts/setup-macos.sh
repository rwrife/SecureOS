#!/usr/bin/env bash
set -euo pipefail

# SecureOS macOS bootstrap script
# Installs a minimal OS-dev + validation toolchain for:
# - containerized deterministic builds
# - QEMU boot/test harness
# - GitHub Actions + gh workflow integration

if [[ "${OSTYPE:-}" != darwin* ]]; then
  echo "❌ This script is for macOS only."
  exit 1
fi

log() { printf "\n==> %s\n" "$*"; }
warn() { printf "⚠️  %s\n" "$*"; }
ok() { printf "✅ %s\n" "$*"; }

need_cmd() {
  command -v "$1" >/dev/null 2>&1
}

install_brew_if_missing() {
  if need_cmd brew; then
    ok "Homebrew already installed"
    return
  fi

  log "Installing Homebrew"
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

  # shellcheck disable=SC2016
  if [[ -x /opt/homebrew/bin/brew ]]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
  elif [[ -x /usr/local/bin/brew ]]; then
    eval "$(/usr/local/bin/brew shellenv)"
  fi

  need_cmd brew || { echo "❌ Homebrew install failed"; exit 1; }
  ok "Homebrew installed"
}

ensure_xcode_clt() {
  if xcode-select -p >/dev/null 2>&1; then
    ok "Xcode Command Line Tools already installed"
  else
    log "Installing Xcode Command Line Tools"
    xcode-select --install || true
    warn "If a popup appeared, complete installation and re-run this script."
    exit 1
  fi
}

install_formulae() {
  local formulae=(
    llvm
    nasm
    cmake
    ninja
    make
    python
    qemu
    xorriso
    mtools
    git
    gh
    jq
    shellcheck
  )

  log "Installing/updating Homebrew formulae"
  brew update
  brew install "${formulae[@]}"
  ok "Core toolchain installed"
}

setup_docker_runtime() {
  # Prefer existing Docker engine if available
  if docker info >/dev/null 2>&1; then
    ok "Docker engine is already available"
    return
  fi

  log "Docker engine not available; installing lightweight Colima runtime"
  brew install colima docker

  if ! pgrep -f "colima" >/dev/null 2>&1; then
    colima start --cpu 4 --memory 6 --disk 40
  fi

  # Give it a brief moment
  sleep 3

  if docker info >/dev/null 2>&1; then
    ok "Docker + Colima ready"
  else
    warn "Docker still unavailable. You can alternatively install Docker Desktop."
    warn "Try: brew install --cask docker && open -a Docker"
  fi
}

print_versions() {
  log "Tool versions"
  {
    echo "clang: $(clang --version | head -n1 || true)"
    echo "llvm-config: $(llvm-config --version 2>/dev/null || true)"
    echo "nasm: $(nasm -v || true)"
    echo "cmake: $(cmake --version | head -n1 || true)"
    echo "ninja: $(ninja --version || true)"
    echo "python3: $(python3 --version || true)"
    echo "qemu-x86_64: $(qemu-system-x86_64 --version | head -n1 || true)"
    echo "qemu-aarch64: $(qemu-system-aarch64 --version | head -n1 || true)"
    echo "xorriso: $(xorriso -version 2>/dev/null | head -n1 || true)"
    echo "mtools (mcopy): $(mcopy -V 2>&1 | head -n1 || true)"
    echo "git: $(git --version || true)"
    echo "gh: $(gh --version | head -n1 || true)"
    echo "docker: $(docker --version || true)"
  } | sed 's/^/  /'
}

verify_basics() {
  log "Running basic verification checks"

  local missing=0
  for cmd in clang nasm cmake ninja python3 qemu-system-x86_64 qemu-system-aarch64 xorriso mcopy git gh; do
    if need_cmd "$cmd"; then
      ok "Found: $cmd"
    else
      echo "❌ Missing: $cmd"
      missing=1
    fi
  done

  if docker info >/dev/null 2>&1; then
    ok "Docker engine reachable"
  else
    warn "Docker engine not reachable yet"
    missing=1
  fi

  if [[ $missing -ne 0 ]]; then
    warn "Some checks failed. See messages above."
    return 1
  fi

  ok "All baseline checks passed"
}

next_steps() {
  cat <<'EOF'

🎉 SecureOS macOS bootstrap complete.

Suggested next steps in this repo:
  1) Create toolchain container:
     - build/docker/Dockerfile.toolchain
     - build/toolchain.lock

  2) Add deterministic wrappers:
     - build/scripts/build.sh
     - build/scripts/test.sh
     - build/scripts/run_qemu.sh

  3) Add first validation slice:
     - x86 boot to kmain
     - serial log + TEST markers
     - isa-debug-exit pass/fail

  4) Add CI workflow to run the same containerized commands.

If gh auth is needed:
  gh auth login
EOF
}

main() {
  log "SecureOS macOS environment bootstrap"
  ensure_xcode_clt
  install_brew_if_missing
  install_formulae
  setup_docker_runtime
  print_versions
  verify_basics
  next_steps
}

main "$@"
