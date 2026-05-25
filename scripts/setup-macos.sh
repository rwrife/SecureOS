#!/usr/bin/env bash
# setup-macos.sh - Install Docker and QEMU on macOS
#
# This script installs the two host dependencies needed to build and run
# SecureOS: Docker and QEMU. All other toolchain tools (compiler, linker,
# etc.) live inside the Docker container.
set -euo pipefail

if [[ "${OSTYPE:-}" != darwin* ]]; then
  echo "ERROR: This script is for macOS only."
  exit 1
fi

log() { printf "\n==> %s\n" "$*"; }
ok()  { printf "  ✓ %s\n" "$*"; }
err() { printf "  ✗ %s\n" "$*"; }

need_cmd() { command -v "$1" >/dev/null 2>&1; }

# --- Homebrew ---
install_brew_if_missing() {
  if need_cmd brew; then
    ok "Homebrew already installed"
    return
  fi

  log "Installing Homebrew"
  /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

  if [[ -x /opt/homebrew/bin/brew ]]; then
    eval "$(/opt/homebrew/bin/brew shellenv)"
  elif [[ -x /usr/local/bin/brew ]]; then
    eval "$(/usr/local/bin/brew shellenv)"
  fi

  need_cmd brew || { err "Homebrew install failed"; exit 1; }
  ok "Homebrew installed"
}

# --- Docker ---
install_docker() {
  if docker info >/dev/null 2>&1; then
    ok "Docker already available"
    return
  fi

  log "Installing Docker runtime"

  # Prefer existing Docker Desktop
  if [[ -d "/Applications/Docker.app" ]]; then
    echo "  Docker Desktop found but not running. Please start it."
    open -a Docker
    echo "  Waiting for Docker to start..."
    local retries=30
    while ! docker info >/dev/null 2>&1; do
      sleep 2
      retries=$((retries - 1))
      if [[ $retries -le 0 ]]; then
        err "Docker Desktop did not start in time."
        exit 1
      fi
    done
    ok "Docker Desktop started"
    return
  fi

  # Install via Homebrew (colima for lightweight runtime)
  echo "  Installing colima + docker CLI..."
  brew install colima docker

  colima start --cpu 4 --memory 6 --disk 40
  sleep 3

  if docker info >/dev/null 2>&1; then
    ok "Docker (colima) ready"
  else
    err "Docker still unavailable after colima start."
    echo "  Alternative: brew install --cask docker && open -a Docker"
    exit 1
  fi
}

# --- QEMU ---
install_qemu() {
  if need_cmd qemu-system-x86_64; then
    ok "QEMU already installed"
    return
  fi

  log "Installing QEMU"
  brew install qemu
  ok "QEMU installed"
}

# --- Verify ---
verify() {
  log "Verifying installation"
  local failed=0

  if docker info >/dev/null 2>&1; then
    ok "Docker: $(docker --version)"
  else
    err "Docker not accessible"
    failed=1
  fi

  if need_cmd qemu-system-x86_64; then
    ok "QEMU: $(qemu-system-x86_64 --version | head -n1)"
  else
    err "QEMU not found"
    failed=1
  fi

  if [[ $failed -ne 0 ]]; then
    echo ""
    echo "Some checks failed. See messages above."
    exit 1
  fi

  echo ""
  echo "✓ Setup complete! You can now run: ./start.sh"
}

# --- Main ---
main() {
  log "SecureOS macOS Setup"
  install_brew_if_missing
  install_docker
  install_qemu
  verify
}

main "$@"