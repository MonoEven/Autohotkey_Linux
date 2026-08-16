#!/bin/bash
# Find where the virtual-keyboard protocol XML lives and fetch it.
base="https://gitlab.freedesktop.org/api/v4/projects/wayland%2Fwayland-protocols"
echo "--- unstable tree (1.31) ---"
curl -fsSL "${base}/repository/tree?path=unstable&per_page=100&ref=1.31" | grep -o '"name":"[^"]*"' | head -40
