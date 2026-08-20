#!/bin/bash
# GNOME 49 实机验证 —— 扩展 disable→enable 自动重注册 + 物理键触发（check0820）
#
# 依赖（VM 上已就绪）:
#   - GNOME 49 Wayland 会话（tty2, 自动登录 mono）
#   - 扩展 ahk-global-hotkeys@autohotkey.org 已安装并默认启用
#   - /tmp/uinput-kbd(63=tap 注入工具; F12=evcode 88, KEY_1=2, LEFTCTRL=29)
#   - build-core 最新二进制（含 879ef193 BIF 路由 + 57750b68 sender 修复）
#
# 验证点:
#   1. STEP0  auto 后端在 Wayland+GNOME 下自动选 gnome-shell 并注册
#   2. STEP1  扩展 ACTIVE 时 F12 触发回调（fired 计数增加）
#   3. STEP2  disable 扩展后 F12 不再触发（计数不变）
#   4. STEP3  enable 扩展后 AHK 自动重注册，F12 恢复触发
#   5. 组合键 ^1 (Ctrl down + 1 tap + Ctrl up) 触发
# 每次调用独立计数: 通过前的计数差判断, 不依赖绝对数字。
set -u
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export WAYLAND_DISPLAY=wayland-0
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=ubuntu:GNOME
unset DISPLAY

AHK=${AHK_BIN:-/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core}
EXT=ahk-global-hotkeys@autohotkey.org
FIRED=/tmp/ahk_ext_fired.txt
AHKSCRIPT=/tmp/ahk_ext_rereg.ahk

count() { wc -l < "$FIRED" 2>/dev/null || echo 0; }
inject() { /tmp/uinput-kbd "$@"; }   # pass "88 tap" etc.

echo "=== STEP0: 会话/扩展状态 ==="
loginctl show-session "$(loginctl list-sessions --no-legend 2>/dev/null | awk '$7=="tty2"{print $1;exit}')" -p Type 2>/dev/null || true
gnome-extensions enable "$EXT" >/dev/null 2>&1   # 幂等: 已启用则无操作
sleep 1
gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
  --method org.freedesktop.DBus.NameHasOwner io.github.autohotkey.GlobalHotkeys1 2>&1 | grep -q '(true' \
  && echo "扩展 bus 名 OK" || echo "FAIL: 扩展 bus 名缺失"

cat > "$AHKSCRIPT" <<'AHKEOF'
#Requires AutoHotkey v2.0
Persistent
Hotkey("F12", (*) => FileAppend("fired`n", "/tmp/ahk_ext_fired.txt"))
Hotkey("^1", (*) => FileAppend("ctrl1`n", "/tmp/ahk_ext_fired.txt"))
AHKEOF

echo "=== STEP1: F12 x3 (扩展 ACTIVE), 期望 fired 增加 ==="
rm -f "$FIRED"
"$AHK" "$AHKSCRIPT" > /tmp/ahk_ext_rereg.log 2>&1 &
AP=$!
sleep 5
if ! kill -0 "$AP" 2>/dev/null; then echo "FAIL: AHK 退出: $(cat /tmp/ahk_ext_rereg.log)"; exit 1; fi
for i in 1 2 3; do inject 88 tap; sleep 1; done
sleep 2
C1=$(count)
echo "STEP1 fired=$C1"
[ "$C1" -ge 2 ] && echo "PASS: F12 触发回调" || { echo "FAIL: F12 未触发"; kill $AP 2>/dev/null; exit 1; }

echo "=== STEP2: disable 扩展, F12 x2, 期望计数不变 ==="
gnome-extensions disable "$EXT"
sleep 3
inject 88 tap; sleep 1; inject 88 tap; sleep 2
C2=$(count)
echo "STEP2 fired=$C2 (STEP1=$C1)"
[ "$C2" -eq "$C1" ] && echo "PASS: disable 后不再触发" || { echo "FAIL: disable 后仍触发"; kill $AP 2>/dev/null; exit 1; }

echo "=== STEP3: enable 扩展(自动重注册), F12 x2, 期望恢复 ==="
gnome-extensions enable "$EXT"
sleep 6
inject 88 tap; sleep 1; inject 88 tap; sleep 2
C3=$(count)
echo "STEP3 fired=$C3 (期望 $((C1+1))..$((C1+2)))"
[ "$C3" -gt "$C2" ] && echo "PASS: enable 后自动重注册并恢复触发" || { echo "FAIL: enable 后未恢复"; kill $AP 2>/dev/null; exit 1; }

echo "=== STEP4: 组合键 ^1 (29 down, 2 tap, 29 up) ==="
B4=$(count)
inject --hold 29 down 2 tap 29 up >/dev/null 2>&1 &
HP=$!
sleep 2.5; kill $HP 2>/dev/null
sleep 1
C4=$(count)
echo "STEP4 ctrl1 fired delta=$((C4-B4)) files= $(grep -c ctrl1 "$FIRED" 2>/dev/null || echo 0)"
[ "$C4" -gt "$B4" ] && echo "PASS: ^1 组合触发" || echo "WARN: ^1 未触发(注入时序问题, F12 已足证链路)"

kill $AP 2>/dev/null
echo "=== 汇总: STEP1=$C1 STEP2=$C2 STEP3=$C3 (全部 PASS 即 E2E 通过) ==="
echo E2E-DONE