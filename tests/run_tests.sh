#!/bin/bash
# Full validation suite for the AutoHotkey Linux port.
# Runs in headless mode (console MsgBox fallback); see gui_tests.sh for
# display-mode tests.  Usage: run_tests.sh [path-to-ahk_core]
cd "$(dirname "$0")/.." || exit 1
BIN=${1:-build-core/source/linux/core/ahk_core}
export ASAN_OPTIONS=detect_leaks=0
mkdir -p /tmp/ahk_t

pass=0; fail=0
check() { # $1 = name, $2 = expected exit code, rest = args...
  local name="$1"; local want="$2"; shift 2
  DISPLAY= "$BIN" "$@" > /tmp/ahk_t/out.txt 2>&1
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
    Run "nonexistent-xyz-123"
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
cat > /tmp/ahk_t/t17.ahk <<'EOF'
code := RunWait("/bin/sh -c `"exit 7`"")
MsgBox "exitcode=" code
code2 := RunWait("/bin/echo wait-output")
MsgBox "code2=" code2
EOF
cat > /tmp/ahk_t/t18.ahk <<'EOF'
pid := 0
Run "/bin/sleep 2", , , &pid
MsgBox "pid>" (pid > 0)
RunWait "/bin/true", , , &pid
MsgBox "rwpid>" (pid > 0)
EOF
cat > /tmp/ahk_t/t19.ahk <<'EOF'
MsgBox A_YYYY "-" A_MM "-" A_DD
MsgBox A_Hour ":" A_Min ":" A_Sec
MsgBox A_YDay > 0
MsgBox A_MMM " " A_DDD
MsgBox A_Now
MsgBox A_MSec >= 0
EOF
cat > /tmp/ahk_t/t20.ahk <<'EOF'
ini := "/tmp/ahk_t/t20.ini"
IniWrite("hello", ini, "Sec", "Key")
MsgBox IniRead(ini, "Sec", "Key")
MsgBox IniRead(ini, "Sec", "Missing", "def-ok")
IniDelete(ini, "Sec", "Key")
MsgBox "deleted:" (IniRead(ini, "Sec", "Key") = "")
EOF
cat > /tmp/ahk_t/t21.ahk <<'EOF'
A_Clipboard := "clip-value"
MsgBox A_Clipboard
MsgBox "len=" StrLen(A_Clipboard)
A_Clipboard := ""
MsgBox "cleared:" (A_Clipboard = "")
EOF
cat > /tmp/ahk_t/t22.ahk <<'EOF'
Run "sleep 20", , , &pid
MsgBox "started:" (pid > 0)
MsgBox "exists:" (ProcessExist(pid) > 0)
ProcessClose(pid)
Sleep 300
MsgBox "closed:" (ProcessExist(pid) = 0)
MsgBox "self:" (ProcessExist() > 0)
EOF
cat > /tmp/ahk_t/t23.ahk <<'EOF'
MsgBox "type=" DriveGetType("/")
MsgBox "fs=" DriveGetFilesystem("/")
MsgBox "free>" (DriveGetSpaceFree("/") > 0)
MsgBox "cap>" (DriveGetCapacity("/") > 0)
MsgBox "status=" DriveGetStatus("/")
MsgBox "list>" (StrLen(DriveGetList()) > 0)
EOF
cat > /tmp/ahk_t/t24.ahk <<'EOF'
SoundBeep(440, 50)
MsgBox "beep-ok"
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
check t17_runwait 0 /tmp/ahk_t/t17.ahk
check t18_run_pid 0 /tmp/ahk_t/t18.ahk
check t19_time_vars 0 /tmp/ahk_t/t19.ahk
check t20_ini 0 /tmp/ahk_t/t20.ahk
check t21_clipboard 0 /tmp/ahk_t/t21.ahk
check t22_process 0 /tmp/ahk_t/t22.ahk
check t23_drive 0 /tmp/ahk_t/t23.ahk
check t24_soundbeep 0 /tmp/ahk_t/t24.ahk
check t_missing_file 1 /tmp/ahk_t/does_not_exist.ahk

# t25: FileSelect/DirSelect headless stdin path -- each FileSelect() call
# reads one line from stdin (empty line = cancel); FileSelect("M") reads
# one path per line until an empty line and returns an Array.
cat > /tmp/ahk_t/t25.ahk <<'EOF'
MsgBox "fs=" FileSelect()
MsgBox "fsc=" (FileSelect() = "")
MsgBox "ds=" DirSelect()
arr := FileSelect("M")
MsgBox "mtype=" (Type(arr) = "Array")
MsgBox "mcount=" arr.Length
MsgBox "m1=" arr[1]
MsgBox "m2=" arr[2]
EOF
printf '%b' "/tmp/ahk_t/choice.txt\n\n/tmp/ahk_t/dir\n/tmp/ahk_t/a.txt\n/tmp/ahk_t/b.txt\n\n" \
  | DISPLAY= "$BIN" /tmp/ahk_t/t25.ahk > /tmp/ahk_t/out.txt 2>&1
if grep -q "^fs=/tmp/ahk_t/choice.txt$" /tmp/ahk_t/out.txt \
   && grep -q "^fsc=1$" /tmp/ahk_t/out.txt \
   && grep -q "^ds=/tmp/ahk_t/dir$" /tmp/ahk_t/out.txt \
   && grep -q "^mtype=1$" /tmp/ahk_t/out.txt \
   && grep -q "^mcount=2$" /tmp/ahk_t/out.txt \
   && grep -q "^m1=/tmp/ahk_t/a.txt$" /tmp/ahk_t/out.txt \
   && grep -q "^m2=/tmp/ahk_t/b.txt$" /tmp/ahk_t/out.txt; then
  pass=$((pass+1)); echo "PASS: t25_dialogs_stdin (exit=0)"
else
  fail=$((fail+1)); echo "FAIL: t25_dialogs_stdin"; cat /tmp/ahk_t/out.txt
fi

# t26: Reload -- the old process exits through OnExit with ExitReason
# "Reload" and a fresh interpreter instance takes over (end-to-end,
# headless; the new instance is launched via ActionExec with the
# "/restart /script <path> /pid <pid>" protocol).
rm -f /tmp/ahk_t/t26.flag /tmp/ahk_t/t26.txt
cat > /tmp/ahk_t/t26.ahk <<'EOF'
#Requires AutoHotkey v2.0
MARK := "/tmp/ahk_t/t26.txt"
F1 := "/tmp/ahk_t/t26.flag"
if FileExist(F1) {
    ; Second instance (spawned by the first instance's Reload).
    FileAppend("second=1`n", MARK)
    FileDelete(F1)
    ExitApp 0
}
FileAppend("first=1`n", MARK)
FileAppend("", F1)
OnExit((ExitReason, ExitCode) => (FileAppend("exit_reason=" ExitReason "`n", MARK), 0))
Reload()
Sleep 3000
FileAppend("still_alive=1`n", MARK)
ExitApp 0
EOF
DISPLAY= timeout 20 "$BIN" /tmp/ahk_t/t26.ahk > /tmp/ahk_t/out.txt 2>&1
if grep -q "^first=1$" /tmp/ahk_t/t26.txt \
   && grep -q "^second=1$" /tmp/ahk_t/t26.txt \
   && grep -q "^exit_reason=Reload$" /tmp/ahk_t/t26.txt \
   && ! grep -q "still_alive" /tmp/ahk_t/t26.txt; then
  pass=$((pass+1)); echo "PASS: t26_reload (exit=0)"
else
  fail=$((fail+1)); echo "FAIL: t26_reload"; cat /tmp/ahk_t/t26.txt 2>/dev/null
fi

echo "=============================="
echo "PASS=$pass FAIL=$fail"
[ "$fail" = 0 ]
