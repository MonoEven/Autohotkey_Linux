# ControlSendText

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:181](../../tests/doccheck/assert_ctrl.ahk#L181)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("send_keys=" (downs(sd_lines) = "a,b" ? 1 : 0))
Log("send_win=" (wins(sd_lines) = ehwnd ? 1 : 0))
ControlSendText("{x}", "Edit1", "CtlMain")
Sleep(80)
Log("sendtext=" (downs(next_lines()) = "Shift_L,braceleft,x,Shift_L,braceright" ? 1 : 0))
````
