; --diag diagnostic regression (check_detail0821 §12/R1-3 + §1.2-C/R1-6).
; The diagnostic must run headless (no display, no session bus with a
; portal/GNOME shell), fail closed on the R1-6 probes (no GlobalShortcuts
; portal backend, unknown GNOME major), and still print every R1-6 line.
#Requires AutoHotkey v2.0

diag_out := "/tmp/ahk_dc_diag_out.txt"
FileDelete(diag_out)
; A_AhkPath is the interpreter running this script (absolute, no spaces in
; the CI runner path); --diag prints to stdout.  Redirect through sh so the
; output is captured to a file the assertions can read back.  (Linux
; CreateProcess parses args Windows-style: only double quotes group.)
RunWait('sh -c "' A_AhkPath ' --diag > ' diag_out ' 2>&1"', , "Hide")
txt := FileRead(diag_out)

MsgBox "diag_ran=" (InStr(txt, "=== AutoHotkey Linux diagnostic ===") ? 1 : 0)
MsgBox "diag_header=" (InStr(txt, "=== end diagnostic ===") ? 1 : 0)
MsgBox "diag_input_backend=" (InStr(txt, "input-backend:") ? 1 : 0)
MsgBox "diag_caps_v2=" (InStr(txt, "caps-version=2")
    && InStr(txt, "input-event-version: 1")
    && InStr(txt, "scan_code=") && InStr(txt, "synthetic_provenance=") ? 1 : 0)
MsgBox "diag_gnome_major_line=" (InStr(txt, "gnome-major  :") ? 1 : 0)
; Fail-closed in CI/headless: no org.gnome.Shell on the bus and no
; gnome-shell-version file => "0 (unknown)".
MsgBox "diag_gnome_major_unknown=" (InStr(txt, "gnome-major  : 0") ? 1 : 0)
; Fail-closed: the CI doc-check bus is a bare dbus-daemon, no portal.
MsgBox "diag_portal_gs_no=" (InStr(txt, "portal-global-shortcuts : no") ? 1 : 0)
MsgBox "diag_appid_line=" (InStr(txt, "portal-app-id-resolvable :") ? 1 : 0)
; §3 (check_detail0821 §2.2-A): the XI2 sourceid / XTEST device diagnostic
; lines must be present.  Headless (no display) the probe fails closed
; (no sourceid tap, no XTEST device) -- the lines still print.
MsgBox "diag_xi2_line=" (InStr(txt, "xi2-sourceid    :") ? 1 : 0)
MsgBox "diag_xtest_line=" (InStr(txt, "xi2-xtest-dev   :") ? 1 : 0)
MsgBox "diag_libei_build_line=" (InStr(txt, "libei-build  :")
    && InStr(txt, "liboeffis=") && InStr(txt, "libportal=") ? 1 : 0)
MsgBox "diag_libei_state_line=" (InStr(txt, "libei-state  :")
    && InStr(txt, "target=unknown") ? 1 : 0)
