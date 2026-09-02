#!/bin/bash
# ci-container-deps.sh -- install the build/test deps for the container
# matrix (fedora/arch/debian/ubuntu) and the host jobs.  check_detail0821
# §16-1 / R4.  Runs as root inside containers; uses sudo on the host.
set -e
SUDO=""
if [ "$(id -u)" != "0" ]; then SUDO="sudo"; fi
if [ -f /etc/os-release ]; then . /etc/os-release; fi
case "$ID" in
  fedora)
    $SUDO dnf install -y --setopt=install_weak_deps=False \
      git cmake gcc-c++ make ninja-build \
      libX11-devel libXext-devel libXrandr-devel libXinerama-devel libXtst-devel libXi-devel libXfixes-devel \
      wayland-devel wayland-protocols-devel libxkbcommon-devel libxkbcommon-x11-devel \
      libffi-devel dbus-devel gtk3-devel zlib-devel libjpeg-turbo-devel \
      glibc-devel
    $SUDO dnf install -y pkgconf-pkg-config 2>/dev/null || true
    $SUDO dnf install -y --setopt=install_weak_deps=False \
      libei-devel libeis-devel liboeffis-devel 2>/dev/null || \
      echo "optional libei development packages unavailable"
    $SUDO dnf install -y xorg-x11-server-Xvfb xdotool xorg-x11-xauth xorg-x11-xkb-utils 2>/dev/null || \
      echo "xvfb unavailable (smoke will skip)"
    ;;
  arch)
    $SUDO pacman -Sy --noconfirm --needed \
      git cmake gcc make ninja pkgconf \
      libx11 libxext libxrandr libxinerama libxtst libxi libxfixes \
      wayland wayland-protocols libxkbcommon libxkbcommon-x11 \
      libffi dbus gtk3 zlib libjpeg-turbo
    # xvfb is best-effort (only the display smoke needs it); the name varies
    # across Arch versions/mirrors.
    $SUDO pacman -Sy --noconfirm --needed xorg-xvfb xdotool xorg-xkbcomp 2>/dev/null || \
      $SUDO pacman -Sy --noconfirm --needed xorg-server-xvfb xdotool xorg-xkbcomp 2>/dev/null || \
      echo "xvfb unavailable (smoke will skip)"
    $SUDO pacman -Sy --noconfirm --needed xorg-xauth 2>/dev/null || true
    $SUDO pacman -Sy --noconfirm --needed libei 2>/dev/null || \
      echo "optional libei development package unavailable"
    ;;
  debian|ubuntu)
    export DEBIAN_FRONTEND=noninteractive
    $SUDO apt-get update -y
    $SUDO apt-get install -y --no-install-recommends \
      git cmake g++ make ninja-build pkg-config \
      libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxtst-dev libxi-dev libxfixes-dev \
      libwayland-dev wayland-protocols libxkbcommon-dev libxkbcommon-x11-dev \
      libffi-dev libdbus-1-dev libgtk-3-dev zlib1g-dev libjpeg-dev
    $SUDO apt-get install -y --no-install-recommends xvfb xdotool xauth x11-xkb-utils 2>/dev/null || \
      echo "xvfb unavailable (smoke will skip)"
    # sway/weston are needed by the Wayland doc-check suite (the no-xwayland
    # host job); best-effort -- the container jobs only run headless + smoke.
    $SUDO apt-get install -y --no-install-recommends sway xwayland weston 2>/dev/null || \
      echo "sway unavailable (Wayland suite will fail)"
    $SUDO apt-get install -y --no-install-recommends \
      libei-dev libeis-dev liboeffis-dev 2>/dev/null || \
      echo "optional libei development packages unavailable"
    ;;
  *)
    echo "unsupported distro: $ID" >&2
    exit 1
    ;;
esac
echo "deps installed for $ID"