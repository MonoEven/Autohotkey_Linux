#!/bin/bash
# callback_fp scenario: typed CallbackCreate (Float/Double) through a C .so.
set -u
AHK="${AHK:?runner must export AHK}"
rm -f /tmp/scn_callback_fp /tmp/scn_cbfp.txt
cat > /tmp/scn_cbtest.c <<'EOF'
typedef double (*cb_fp)(float);
double apply2(cb_fp f, float x) { return f(x); }
double apply3(cb_fp f, float x, double y) { return f(x) + y; }
EOF
gcc -shared -fPIC -o /tmp/scn_libcbtest.so /tmp/scn_cbtest.c 2>/dev/null || exit 0
cat > /tmp/scn_cbfp.ahk <<'EOF'
#Requires AutoHotkey v2.0
fn(x) => x * 2
cb := CallbackCreate(fn, "CDecl Double Float")
r := DllCall("/tmp/scn_libcbtest.so\apply2", "Ptr", cb, "Float", 3.5, "Double")
fn2(x, y) => x + y
cb2 := CallbackCreate(fn2, "CDecl Double Float Double")
r2 := DllCall("/tmp/scn_libcbtest.so\apply3", "Ptr", cb2, "Float", 1.5, "Double", 2.25, "Double")
CallbackFree(cb)
CallbackFree(cb2)
ok := (Abs(r - 7.0) < 0.001 && Abs(r2 - 6.0) < 0.001)
FileAppend("r=" r " r2=" r2 " ok=" (ok ? 1 : 0) "`n", "/tmp/scn_cbfp.txt")
ExitApp
EOF
"$AHK" /tmp/scn_cbfp.ahk >/dev/null 2>&1
if [ -f /tmp/scn_cbfp.txt ] && grep -q 'ok=1' /tmp/scn_cbfp.txt; then
  touch /tmp/scn_callback_fp
fi
exit 0