#!/usr/bin/env bash
# setup-linux.sh - Install Docker and QEMU on Linux (Debian/Ubuntu/Fedora/RHEL)
#
# This script installs the two host dependencies needed to build and run
# SecureOS: Docker Engine and QEMU. All other toolchain tools (compiler,
# linker, etc.) live inside the Docker container.
set -euo pipefail

log() { printf "\n==> %s\n" "$*"; }
ok()  { printf "  ✓ %s\n" "$*"; }
err() { printf "  ✗ %s\n" "$*"; }

detect_distro() {
  if [ -f /etc/os-release ]; then
    . /etc/os-release
    echo "$ID"
  elif command -v lsb_release >/dev/null 2>&1; then
    lsb_release -si | tr '[:upper:]' '[:lower:]'
  else
    echo "unknown"
  fi
}

install_docker_debian() {
  log "Installing Docker Engine (Debian/Ubuntu)"
  sudo apt-get update -qq
  sudo apt-get install -y -qq ca-certificates curl gnupg

  sudo install -m 0755 -d /etc/apt/keyrings
  if [ ! -f /etc/apt/keyrings/docker.gpg ]; then
    curl -fsSL https://download.docker.com/linux/$1/gpg | sudo gpg --dearmor -o /etc/apt/keyrings/docker.gpg
    sudo chmod a+r /etc/apt/keyrings/docker.gpg
  fi

  local arch
  arch="$(dpkg --print-architecture)"
  echo "deb [arch=$arch signed-by=/etc/apt/keyrings/docker.gpg] https://download.docker.com/linux/$1 $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | \
    sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

  sudo apt-get update -qq
  sudo apt-get install -y -qq docker-ce docker-ce-cli containerd.io docker-buildx-plugin
}

install_docker_fedora() {
  log "Installing Docker Engine (Fedora/RHEL)"
  sudo dnf -y install dnf-plugins-core
  sudo dnf config-manager --add-repo https://download.docker.com/linux/fedora/docker-ce.repo
  sudo dnf -y install docker-ce docker-ce-cli containerd.io docker-buildx-plugin
  sudo systemctl start docker
  sudo systemctl enable docker
}

install_qemu_debian() {
  log "Installing QEMU"
  sudo apt-get install -y -qq qemu-system-x86
}

install_qemu_fedora() {
  log "Installing QEMU"
  sudo dnf -y install qemu-system-x86
}

add_user_to_docker_group() {
  if ! groups "$USER" | grep -q '\bdocker\b'; then
    log "Adding $USER to docker group"
    sudo usermod -aG docker "$USER"
    echo "  NOTE: You may need to log out and back in for group changes to take effect."
  fi
}

verify() {
  log "Verifying installation"
  local failed=0

  if command -v docker >/dev/null 2>&1; then
    ok "Docker installed: $(docker --version)"
  else
    err "Docker not found"
    failed=1
  fi

  if command -v qemu-system-x86_64 >/dev/null 2>&1; then
    ok "QEMU installed: $(qemu-system-x86_64 --version | head -n1)"
  else
    err "QEMU not found"
    failed=1
  fi

  # Try docker without sudo (may fail if group not yet active)
  if docker info >/dev/null 2>&1; then
    ok "Docker daemon accessible"
  else
    echo "  ⚠ Docker daemon not accessible without sudo."
    echo "    Try: newgrp docker  (or log out and back in)"
  fi

  if [ $failed -ne 0 ]; then
    echo ""
    echo "Some installations failed. See messages above."
    exit 1
  fi

  echo ""
  echo "✓ Setup complete! You can now run: ./start.sh"
}

main() {
  log "SecureOS Linux Setup"

  local distro
  distro="$(detect_distro)"
  echo "  Detected distro: $distro"

  # Install Docker if not present
  if command -v docker >/dev/null 2>&1; then
    ok "Docker already installed"
  else
    case "$distro" in
      ubuntu|debian)
        install_docker_debian "$distro"
        ;;
      fedora|rhel|centos)
        install_docker_fedora
        ;;
      *)
        echo "  Unsupported distro: $distro"
        echo "  Install Docker manually: https://docs.docker.com/engine/install/"
        exit 1
        ;;
    esac
  fi

  # Install QEMU if not present
  if command -v qemu-system-x86_64 >/dev/null 2>&1; then
    ok "QEMU already installed"
  else
    case "$distro" in
      ubuntu|debian)
        install_qemu_debian
        ;;
      fedora|rhel|centos)
        install_qemu_fedora
        ;;
      *)
        echo "  Install QEMU manually: sudo apt install qemu-system-x86"
        exit 1
        ;;
    esac
  fi

  add_user_to_docker_group
  verify
}

main "$@"
