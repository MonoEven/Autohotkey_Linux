#!/bin/bash
# System clipboard test on Xvfb, v2: writer stays alive while xclip reads.
set -x
pkill -f "Xvfb :99" 2>/dev/null; rm -f /tmp/.X99-lock; sleep 0.3
Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 &
XPID=$!
sleep 1
export DISPLAY=:99
cd /mnt/f/AI/Codex/Autohotkey_Linux || exit 1

echo "=== 1) AHK writes, stays alive; xclip reads ==="
rm -f /tmp/clip_out.txt /tmp/xclip_read.txt
cat > /tmp/clip_writer.ahk <<'EOF'
#Requires AutoHotkey v2.0
A_Clipboard := "Hello from AHK Linux!"
; stay alive 6s so the selection owner keeps serving requests
Sleep(6000)
ExitApp(0)
EOF
timeout 30 build-core/source/linux/core/ahk_core /tmp/clip_writer.ahk &
WPID=$!
sleep 1.5
timeout 5 xclip -selection clipboard -o > /tmp/xclip_read.txt 2>/dev/null
echo "xclip got: [$(cat /tmp/xclip_read.txt)]"
wait $WPID

echo "=== 2) AHK self read after write ==="
rm -f /tmp/clip_out.txt
cat > /tmp/clip_self.ahk <<'EOF'
#Requires AutoHotkey v2.0
A_Clipboard := "self-test-42"
Sleep(500)
FileAppend("self=" A_Clipboard "`n", "/tmp/clip_out.txt")
ExitApp(0)
EOF
timeout 30 build-core/source/linux/core/ahk_core /tmp/clip_self.ahk
cat /tmp/clip_out.txt

echo "=== 3) xclip writes; AHK reads ==="
printf 'From xclip!' | timeout 5 xclip -selection clipboard -i
cat > /tmp/clip_reader.ahk <<'EOF'
#Requires AutoHotkey v2.0
Sleep(300)
FileAppend("xclip=" A_Clipboard "`n", "/tmp/clip_out2.txt")
ExitApp(0)
EOF
timeout 30 build-core/source/linux/core/ahk_core /tmp/clip_reader.ahk
cat /tmp/clip_out2.txt 2>/dev/null
kill $XPID 2>/dev/null
