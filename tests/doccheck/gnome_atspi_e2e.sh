#!/bin/bash
# GNOME 49 AT-SPI 控件自动化端到端验证（check0820 direction-B item 2）
#
# 依赖（VM 上已就绪）:
#   - GNOME 49 Wayland 会话 + build-core 最新二进制
#   - /tmp/gtk-atspi-test: 本地编译的 GTK3 测试应用
#     (窗口 "AHK AT-SPI Test"; 标签初始 "Hello AT-SPI";
#      按钮 "Push Me", 点击后标签改为 "clicked-yes")
#    源码见 tests/doccheck/gtk_atspi_test.c
#
# 验证点 (Control* 在 Wayland 会话自动走 AT-SPI 后端):
#   1. ControlGetText 读取标签/按钮 accessible 文本
#   2. ControlClick("Push Me") -> Action.DoAction("click"/0)
#   3. 点击后旧文本消失、新文本出现 (端到端状态变化)
set -u
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export WAYLAND_DISPLAY=wayland-0
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=ubuntu:GNOME
unset DISPLAY

AHK=${AHK_BIN:-/home/mono/Autohotkey_Linux/build-core/source/linux/core/ahk_core}
APP=/tmp/gtk-atspi-test
OUT=/tmp/atspi_e2e_out.txt

echo "=== 启动 GTK 测试应用 ==="
pkill -f gtk-atspi-test 2>/dev/null; sleep 0.5
GDK_BACKEND=wayland "$APP" > /tmp/atspi_app.log 2>&1 &
AP=$!
sleep 4
kill -0 "$AP" 2>/dev/null || { echo "FAIL: app did not start"; exit 1; }

cat > /tmp/atspi_e2e.ahk <<'AHKEOF'
#Requires AutoHotkey v2.0
Persistent(True)
before := ControlGetText("Hello AT-SPI", "AHK AT-SPI Test")
FileAppend("before=[" before "]" "`n", "/tmp/atspi_e2e_out.txt")
ControlClick("Push Me", "AHK AT-SPI Test")
Sleep(1200)
afterNew := ControlGetText("clicked-yes", "AHK AT-SPI Test")
FileAppend("after_new=[" afterNew "]" "`n", "/tmp/atspi_e2e_out.txt")
afterOld := ControlGetText("Hello AT-SPI", "AHK AT-SPI Test")
FileAppend("after_old=[" afterOld "] (empty = changed)" "`n", "/tmp/atspi_e2e_out.txt")
ExitApp(0)
AHKEOF

rm -f "$OUT"
timeout 20 "$AHK" /tmp/atspi_e2e.ahk > /tmp/atspi_ahk.log 2>&1
echo "ahk rc=$?"
echo "=== 结果 ==="
cat "$OUT" 2>/dev/null || echo "(no output)"
echo "=== AHK stderr ==="
cat /tmp/atspi_ahk.log

# 断言
B=$(grep -oP '^before=\[\K[^]]*' "$OUT" 2>/dev/null)
AN=$(grep -oP '^after_new=\[\K[^]]*' "$OUT" 2>/dev/null)
AO=$(grep -oP '^after_old=\[\K[^]]*' "$OUT" 2>/dev/null)
[ "$B" = "Hello AT-SPI" ] && echo "PASS: 读控件文本" || echo "FAIL: 读文本 before=[$B]"
[ "$AN" = "clicked-yes" ] && echo "PASS: DoAction 后新文本" || echo "FAIL: after_new=[$AN]"
[ -z "$AO" ] && echo "PASS: 旧文本消失" || echo "FAIL: after_old=[$AO]"

kill "$AP" 2>/dev/null
echo E2E-DONE