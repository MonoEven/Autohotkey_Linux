# ControlSend

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:176](../../tests/doccheck/assert_ctrl.ahk#L176)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Sends simulated keystrokes or text to a window or control.

## Syntax

````text
ControlSend Keys , ControlID, WinTitle, WinText, ExcludeTitle, ExcludeText ControlSendText Keys , ControlID, WinTitle, WinText, ExcludeTitle, ExcludeText
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- ControlSend / ControlSendText (keystrokes reach the control). ---
ControlSend("ab", "Edit1", "CtlMain")
Sleep(80)
sd_lines := next_lines()
Log("send_keys=" (downs(sd_lines) = "a,b" ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/ControlSend.htm](../../docs-v2/docs/lib/ControlSend.htm)

````ahk
Run "Notepad",, "Min", &PID  ; Run Notepad minimized.
WinWait "ahk_pid " PID  ; Wait for it to appear.
; Send the text to the inactive Notepad edit control.
; The third parameter is omitted so the last found window is used.
ControlSend "This is a line of text in the notepad window.{Enter}",
````
