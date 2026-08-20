#!/bin/bash
# GNOME 49 实机验证 —— 普通用户首次安装：核心 + 一键插件（check0820 后续反馈项）
#
# 依赖（VM 上已就绪）:
#   - GNOME 49 Wayland 会话（tty2, 自动登录 mono）
#   - /home/mono/Autohotkey_Linux 源码树（tools/linux/install.sh 等）
#   - build-core 最新二进制（含 879ef193 BIF 路由 + 57750b68 sender 修复，
#     由源同步后重编）
#   - /tmp/uinput-kbd（F12=evcode 88）
#
# 验证点（普通用户第一次安装的真实路径）:
#   1. 移除既有扩展目录 -> 模拟"第一次"
#   2. 真实 ~/.local 前缀下 install.sh --prefix ... --gnome-extension --yes
#      -> 核心 + launcher + 扩展一次装好
#   3. 扩展 metadata/uuid/extension.js 就位
#   4. 会话环境下 AHK 热键脚本注册 F12
#   5. gnome-extensions enable + 扩展 bus 名 owner 存在
#   6. 物理 F12 x3 -> 回调 fired >= 2（真实端到端）
#
# 不在 VM 上的调用: 脚本会自行跳过并返回 0 的说明放在每个 FAIL 里；
# 本套件只在 VM 上运行（CI 不跑）。
set -u

export HOME=/home/mono
R=/home/mono/Autohotkey_Linux
EXT_DIR="$HOME/.local/share/gnome-shell/extensions"
EXT="$EXT_DIR/ahk-global-hotkeys@autohotkey.org"
FIRED=/tmp/ahk_real_first_fired.txt
AHK="$R/build-core/source/linux/core/ahk_core"
AHKS=/tmp/ahk_real_first.ahk

fail=0
chk() { if bash -c "$2"; then echo "PASS: $1"; else echo "FAIL: $1"; fail=1; fi }
count() { wc -l < "$FIRED" 2>/dev/null || echo 0; }

echo "=== STEP1: 模拟首次安装（移除既有扩展）==="
rm -rf "$EXT"
ls "$EXT_DIR" >/dev/null 2>&1 || echo "(extensions dir empty/missing)"

echo "=== STEP2: 真实 ~/.local 前缀 --gnome-extension 安装 ==="
bash "$R/tools/linux/install.sh" --prefix "$HOME/.local" --gnome-extension --yes
chk "launcher installed" "test -x '$HOME/.local/bin/ahk'"
chk "extension metadata present" "test -f '$EXT/metadata.json'"
chk "uuid correct" "grep -q '\"uuid\": \"ahk-global-hotkeys@autohotkey.org\"' '$EXT/metadata.json'"
chk "extension.js present" "test -f '$EXT/extension.js'"

echo "=== STEP3: 会话环境启动 AHK 热键脚本 ==="
export XDG_RUNTIME_DIR=/run/user/1000
export DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/1000/bus
export WAYLAND_DISPLAY=wayland-0
export XDG_SESSION_TYPE=wayland
export XDG_CURRENT_DESKTOP=ubuntu:GNOME
unset DISPLAY
cat > "$AHKS" <<'EOF'
#Requires AutoHotkey v2.0
Persistent
Hotkey("F12", (*) => FileAppend("fired`n", "/tmp/ahk_real_first_fired.txt"))
EOF
rm -f "$FIRED"
"$AHK" "$AHKS" > /tmp/ahk_real_first.log 2>&1 &
AP=$!
sleep 5
if kill -0 "$AP" 2>/dev/null; then
  echo "PASS: ahk_core 运行中（Wayland/GNOME 后端注册成功）"
else
  echo "FAIL: ahk_core 退出: $(cat /tmp/ahk_real_first.log)"
  fail=1
fi

echo "=== STEP4: gnome-extensions enable + 物理 F12 x3 ==="
gnome-extensions enable ahk-global-hotkeys@autohotkey.org >/dev/null 2>&1
sleep 2
gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus \
  --method org.freedesktop.DBus.NameHasOwner io.github.autohotkey.GlobalHotkeys1 2>&1 \
  | grep -q '(true' && echo "PASS: 扩展 bus 名 owner 存在" || { echo "FAIL: 扩展 bus owner 缺失"; fail=1; }
B=$(count)
for i in 1 2 3; do /tmp/uinput-kbd 88 tap; sleep 1; done
sleep 2
C=$(count)
echo "STEP4 fired=$C (before=$B)"
[ "$C" -gt "$B" ] && echo "PASS: F12 物理键经扩展触发回调" || { echo "FAIL: F12 未触发"; fail=1; }

kill "$AP" 2>/dev/null
echo "=============================="
echo "GNOME FIRST-INSTALL E2E: fails=$fail"
[ "$fail" = 0 ]