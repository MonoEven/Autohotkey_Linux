#!/bin/bash
# ci-container-deps.sh -- install the build/test deps inside the container
# matrix (fedora/arch/debian/ubuntu).  check_detail0821 §16-1 / R4.
set -e
if [ -f /etc/os-release ]; then . /etc/os-release; fi
case "$ID" in
  fedora)
    dnf install -y --setopt=install_weak_deps=False \
      git cmake gcc-c++ make ninja-build \
      libX11-devel libXext-devel libXrandr-devel libXinerama-devel libXtst-devel libXi-devel libXfixes-devel \
      wayland-devel wayland-protocols-devel libxkbcommon-devel \
      libffi-devel dbus-devel gtk3-devel zlib-devel libjpeg-turbo-devel \
      xorg-x11-server-Xvfb xdotool xorg-x11-server-utils \
      glibc-devel
    ;;
  arch)
    pacman -Sy --noconfirm --needed \
      git cmake gcc make ninja \
      libx11 libxext libxrandr libxinerama libxtst libxi libxfixes \
      wayland wayland-protocols libxkbcommon \
      libffi dbus gtk3 zlib libjpeg-turbo \
      xorg-xvfb xdotool
    ;;
  debian|ubuntu)
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -y
    apt-get install -y --no-install-recommends \
      git cmake g++ make ninja-build \
      libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxtst-dev libxi-dev libxfixes-dev \
      libwayland-dev wayland-protocols libxkbcommon-dev \
      libffi-dev libdbus-1-dev libgtk-3-dev zlib1g-dev libjpeg-dev \
      xvfb xdotool
    ;;
  *)
    echo "unsupported distro: $ID" >&2
    exit 1
    ;;
esac
echo "deps installed for $ID"