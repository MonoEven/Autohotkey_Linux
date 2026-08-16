#!/bin/bash
cd /mnt/f/AI/Codex/Autohotkey_Linux/source/linux/wayland/protocols || exit 1
for ref in 1.15 1.14 1.13 1.12 1.11 1.10; do
  url="https://raw.githubusercontent.com/wayland-project/wayland-protocols/${ref}/unstable/virtual-keyboard/virtual-keyboard-unstable-v1.xml"
  if curl -fsSL -o virtual-keyboard-unstable-v1.xml "$url"; then
    echo "fetched ref ${ref}"
    break
  fi
done
ls -la virtual-keyboard-unstable-v1.xml 2>/dev/null
grep -c "<interface" virtual-keyboard-unstable-v1.xml 2>/dev/null
