#!/bin/bash
cd /mnt/f/AI/Codex/Autohotkey_Linux/tests/doccheck || exit 1
rm -f /tmp/dbg_menu.txt
DISPLAY= timeout 15 /mnt/f/AI/Codex/Autohotkey_Linux/build-core/source/linux/core/ahk_core dbg_menu.ahk > /tmp/dbg_menu_out.txt 2>&1
rc=$?
echo "RC=$rc"
cat /tmp/dbg_menu.txt 2>/dev/null
echo ---
cat /tmp/dbg_menu_out.txt
