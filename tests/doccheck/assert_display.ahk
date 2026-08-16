; Display/file-shortcut module doc-check (v2 docs: FileCreateShortcut/
; FileGetShortcut, ListVars/ListHotkeys/KeyHistory).  Runs headless: the
; display functions print their text to stdout via the headless MsgBox, and
; the shortcut functions create/parse .desktop and .url files in /tmp.
; StatusBar* is covered in assert_monitor.
#Requires AutoHotkey v2.0

; Headless MsgBox prints to stdout, which run_check.sh captures.
Log(line) => MsgBox(line)

; --- FileCreateShortcut/FileGetShortcut: .desktop (Linux .lnk equivalent). ---
FileCreateShortcut("/usr/bin/gedit", "/tmp/ahk_dc_test.desktop", "/home/user", "--new-window", "Test editor", "/usr/share/icons/gedit.png")
FileGetShortcut("/tmp/ahk_dc_test.desktop", &tg, &wd, &ar, &de, &ic)
Log("desktop_target=" (tg = "/usr/bin/gedit" ? 1 : 0))
Log("desktop_dir=" (wd = "/home/user" ? 1 : 0))
Log("desktop_args=" (ar = "--new-window" ? 1 : 0))
Log("desktop_desc=" (de = "Test editor" ? 1 : 0))
Log("desktop_icon=" (ic = "/usr/share/icons/gedit.png" ? 1 : 0))
; Target with spaces gets quoted in Exec.
FileCreateShortcut("/opt/My App/bin/run", "/tmp/ahk_dc_test2.desktop")
FileGetShortcut("/tmp/ahk_dc_test2.desktop", &tg2)
Log("desktop_quoted=" (tg2 = "/opt/My App/bin/run" ? 1 : 0))

; --- Docs: a .url file is an Internet shortcut (INI format). ---
FileCreateShortcut("https://example.com/page", "/tmp/ahk_dc_test.url")
FileGetShortcut("/tmp/ahk_dc_test.url", &ut)
Log("url_target=" (ut = "https://example.com/page" ? 1 : 0))

; --- Error paths (docs: OSError on failure). ---
try
    FileGetShortcut("/tmp/ahk_dc_no_such.lnk")
catch OSError
    Log("shortcut_missing=1")
try
    FileCreateShortcut("x", "/no/such/dir/x.desktop")
catch OSError
    Log("shortcut_baddir=1")

; --- KeyHistory: MaxEvents validated 0..500 (upstream), stored. ---
try
    KeyHistory(999)
catch ValueError
    Log("keyhistory_bad=1")
KeyHistory(100)

; --- ListVars/ListHotkeys/KeyHistory display (headless -> stdout). ---
MyVar := "hello world"
Count := 42
ObjVar := {a: 1}
ListVars()
ListHotkeys()
KeyHistory()
MsgBox("DISPLAY_DONE=1")
