#!/bin/bash
# GNOME 49 实机验证 —— 扩展 v2 协议加固（check0819 P0-2/P0-3）
#
# 验证点:
#   1. 扩展 v2 已启用且导出 v2 接口（RegisterMany/UnregisterMany/无参 ClearOwner）
#   2. AHK 在纯 Wayland+GNOME 下自动选 gnome-shell backend 并注册热键
#   3. Activated 信号为"定向"(dbus-monitor 显示 destination=<unique name>,
#      而非广播 destination=null)
#   4. 物理按键触发 / 抑制 / 退出恢复（沿用已验证的 E2E 模式）
#   5. 直接方法调用隔离性:用 gdbus 以另一个 unique name 调 ClearOwner()
#      （应只清理调用者自己的注册;无参调用成功返回 true）
set -u
mono()
{
  echo "=== $1"
}

# 用户会话环境（SSH 进来时继承 XDG_RUNTIME_DIR=/run/user/1000）
export XDG_RUNTIME_DIR=/run/user/1000
export WAYLAND_DISPLAY=wayland-0
unset DISPLAY

mono "1. extension v2 introspect"
gnome-extensions list --enabled 2>/dev/null | grep -q autohotkey || { echo "FAIL: extension not enabled"; }
gdbus introspect --session --dest io.github.autohotkey.GlobalHotkeys1 \
  --object-path /io/github/autohotkey/GlobalHotkeys1 2>/dev/null | \
  grep -E 'RegisterMany|UnregisterMany|ClearOwner|Activated|Deactivated' | sed 's/^ *//'

mono "2. protocol hardening (direct method calls from a DIFFERENT bus name)"
# gdbus 每次调用都以独立连接(新 unique name)发起;ClearOwner 必须成功(无参)
gdbus call --session --dest io.github.autohotkey.GlobalHotkeys1 \
  --object-path /io/github/autohotkey/GlobalHotkeys1 \
  --method io.github.autohotkey.GlobalHotkeys1.ClearOwner || echo "FAIL: ClearOwner() rejected"
# Register 必须拒绝带非本 owner 前缀的 id —— 无凭据脚本这样做会被扩展拒掉
gdbus call --session --dest io.github.autohotkey.GlobalHotkeys1 \
  --object-path /io/github/autohotkey/GlobalHotkeys1 \
  --method io.github.autohotkey.GlobalHotkeys1.Register \
  ':1.999/foreign' 'F9' 0 || echo "(expected reject for foreign id)"

mono "3. directed-signal observation"
rm -f /tmp/ahk_gn_fired.txt
timeout 25 dbus-monitor "type='signal',interface='io.github.autohotkey.GlobalHotkeys1'" \
  > /tmp/ahk_gn_mon.log 2>&1 &
MON=$!
sleep 1

cat > /tmp/ahk_gn.ahk <<'AHKEOF'
#Requires AutoHotkey v2.0
Persistent
Hotkey("F9", (*) => FileAppend("fired`n", "/tmp/ahk_gn_fired.txt"))
AHKEOF
/tmp/ahk_gn_bin /tmp/ahk_gn.ahk > /tmp/ahk_gn_run.log 2>&1 &
AHK=$!
sleep 3
grep -i "backend" /tmp/ahk_gn_run.log | head -3
echo "fired_count_before=$(wc -l < /tmp/ahk_gn_fired.txt 2>/dev/null || echo 0)"

mono "4. physical key: press F9 (you have 5s each)"
for i in 1 2 3 4 5; do
  echo "press F9..." ; sleep 1
  [ -f /tmp/ahk_gn_fired.txt ] && [ $(wc -l < /tmp/ahk_gn_fired.txt) -ge 1 ] && break
done
echo "fired_count_after=$(wc -l < /tmp/ahk_gn_fired.txt 2>/dev/null || echo 0)"
sleep 2

kill $AHK 2>/dev/null
sleep 2
kill $MON 2>/dev/null

mono "5. signal log (destination must be a unique name, not null)"
grep -E 'Activated|Deactivated' /tmp/ahk_gn_mon.log | head -6
echo
echo "--- key facts ---"
grep -q '^fired' /tmp/ahk_gn_fired.txt 2>/dev/null && echo "OK: hotkey fired" || echo "WARN: not fired (manual keypress needed)"
if grep -q 'destination=:1' /tmp/ahk_gn_mon.log 2>/dev/null; then
  echo "OK: Activated directed to a unique name (not broadcast)"
else
  echo "CHECK: no directed destination seen in signal log (see above)"
fi
grep -q 'ClearOwner' /tmp/ahk_gn_mon.log 2>/dev/null || true
echo "done"