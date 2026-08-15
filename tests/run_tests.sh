#!/bin/bash
# Full validation suite for the AutoHotkey Linux port.
cd /mnt/f/AI/Codex/Autohotkey_Linux || exit 1
BIN=${1:-build-core/source/linux/core/ahk_core}
export ASAN_OPTIONS=detect_leaks=0
mkdir -p /tmp/ahk_t

pass=0; fail=0
check() { # $1 = name, $2 = expected exit code, rest = args...
  local name="$1"; local want="$2"; shift 2
  "$BIN" "$@" > /tmp/ahk_t/out.txt 2>&1
  local got=$?
  if [ "$got" = "$want" ]; then pass=$((pass+1)); echo "PASS: $name (exit=$got)";
  else fail=$((fail+1)); echo "FAIL: $name (exit=$got, want=$want)"; cat /tmp/ahk_t/out.txt; fi
}

cat > /tmp/ahk_t/t01.ahk <<'EOF'
MsgBox "hello"
EOF
cat > /tmp/ahk_t/t02.ahk <<'EOF'
x := 41
x += 1
MsgBox x
EOF
cat > /tmp/ahk_t/t03.ahk <<'EOF'
sum := 0
loop 10
    sum += A_Index
MsgBox sum
EOF
cat > /tmp/ahk_t/t04.ahk <<'EOF'
o := {name: "ahk", ver: 2}
o.extra := "linux"
MsgBox o.name " " o.ver " " o.extra
EOF
cat > /tmp/ahk_t/t05.ahk <<'EOF'
arr := [10, 20, 30]
total := 0
for v in arr
    total += v
MsgBox total
EOF
cat > /tmp/ahk_t/t06.ahk <<'EOF'
out := "/tmp/ahk_t_f.txt"
FileAppend("line1`n", out)
FileAppend("line2`n", out)
content := FileRead(out)
MsgBox content
MsgBox FileExist(out)
DirCreate("/tmp/ahk_t_dir")
MsgBox DirExist("/tmp/ahk_t_dir")
FileDelete(out)
DirDelete("/tmp/ahk_t_dir", true)
EOF
cat > /tmp/ahk_t/t07.ahk <<'EOF'
MsgBox Format("{1} + {2} = {3}", 2, 3, 2 + 3)
MsgBox StrReplace("Hello World", "World", "Linux")
MsgBox StrLen(SubStr("abcdef", 2, 3))
MsgBox Abs(-5) " " Sqrt(144) " " Round(2.675, 2) " " Mod(17, 5)
EOF
cat > /tmp/ahk_t/t08.ahk <<'EOF'
MsgBox DateAdd("20240101", 3, "days")
MsgBox DateDiff("20240104", "20240101", "days")
EOF
cat > /tmp/ahk_t/t09.ahk <<'EOF'
parts := StrSplit("a,b,c", ",")
MsgBox parts.Length ":" parts[1] ":" parts[3]
EnvSet("AHK_TEST_VAR", "env-ok")
MsgBox EnvGet("AHK_TEST_VAR")
EOF
cat > /tmp/ahk_t/t10.ahk <<'EOF'
MsgBox "before"
Sleep 200
MsgBox "after"
ExitApp 0
EOF
cat > /tmp/ahk_t/t11.ahk <<'EOF'
MsgBox "persistent?"
Persistent()
MsgBox "end"
ExitApp 0
EOF
cat > /tmp/ahk_t/t12.ahk <<'EOF'
OnExit(*) => MsgBox("OnExit ran")
MsgBox "main done"
ExitApp 0
EOF
cat > /tmp/ahk_t/t13.ahk <<'EOF'
Try {
    Run "nonexistent"
} Catch as e {
    MsgBox "caught: " e.Message
}
MsgBox "continues after try"
EOF
cat > /tmp/ahk_t/t14.ahk <<'EOF'
#Requires AutoHotkey v2.0
f := (*) => "from closure"
MsgBox f()
class Greeter {
    static SayHello() => "class hello"
}
MsgBox Greeter.SayHello()
EOF
cat > /tmp/ahk_t/t15.ahk <<'EOF'
hotkey_test := "not a hotkey"
MsgBox IsLabel("nonexistent") " " IsLabel("t15_main")
t15_main:
MsgBox "label works"
ExitApp 0
EOF
cat > /tmp/ahk_t/t16.ahk <<'EOF'
MsgBox A_ScriptName " / " A_ScriptDir
MsgBox A_PtrSize " " A_Is64bitOS
MsgBox A_TickCount > 0
MsgBox A_UserName
EOF

check t01_basic_msgbox 0 /tmp/ahk_t/t01.ahk
check t02_arith 0 /tmp/ahk_t/t02.ahk
check t03_loop_index 0 /tmp/ahk_t/t03.ahk
check t04_object 0 /tmp/ahk_t/t04.ahk
check t05_array_for 0 /tmp/ahk_t/t05.ahk
check t06_files 0 /tmp/ahk_t/t06.ahk
check t07_strings_math 0 /tmp/ahk_t/t07.ahk
check t08_dates 0 /tmp/ahk_t/t08.ahk
check t09_split_env 0 /tmp/ahk_t/t09.ahk
check t10_sleep_exit 0 /tmp/ahk_t/t10.ahk
check t11_persistent 0 /tmp/ahk_t/t11.ahk
check t12_onexit 0 /tmp/ahk_t/t12.ahk
check t13_try_catch 0 /tmp/ahk_t/t13.ahk
check t14_closures_classes 0 /tmp/ahk_t/t14.ahk
check t15_labels 0 /tmp/ahk_t/t15.ahk
check t16_builtin_vars 0 /tmp/ahk_t/t16.ahk
check t_missing_file 1 /tmp/ahk_t/does_not_exist.ahk

echo "=============================="
echo "PASS=$pass FAIL=$fail"
[ "$fail" = 0 ]
