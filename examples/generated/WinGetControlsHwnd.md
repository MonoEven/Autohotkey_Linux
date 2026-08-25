# WinGetControlsHwnd

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:85](../../tests/doccheck/assert_ctrl.ahk#L85)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_win.ahk:81](../../tests/doccheck/assert_win.ahk#L81)

Returns an array of unique IDs (HWNDs) for all controls in the specified window.

## Syntax

````text
HWNDs := WinGetControlsHwnd(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ctrls := WinGetControls("CtlMain")
Log("ctrl_list=" (Join(ctrls) = "Edit1,Edit2,Button1,ComboBox1,Hidden1" ? 1 : 0))
hws := WinGetControlsHwnd("CtlMain")
Log("ctrl_hwnds=" (hws.Length = 5 ? 1 : 0))
Join(arr) {
    out := ""
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetControlsHwnd.htm](../../docs-v2/docs/lib/WinGetControlsHwnd.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
