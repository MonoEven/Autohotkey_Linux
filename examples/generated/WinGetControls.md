# WinGetControls

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:83](../../tests/doccheck/assert_ctrl.ahk#L83)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_win.ahk:80](../../tests/doccheck/assert_win.ahk#L80)

Returns an array of ClassNNs for all controls in the specified window.

## Syntax

````text
ClassNNs := WinGetControls(WinTitle, WinText, ExcludeTitle, ExcludeText)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- WinGetControls / WinGetControlsHwnd (child enumeration + ClassNN). ---
ctrls := WinGetControls("CtlMain")
Log("ctrl_list=" (Join(ctrls) = "Edit1,Edit2,Button1,ComboBox1,Hidden1" ? 1 : 0))
hws := WinGetControlsHwnd("CtlMain")
Log("ctrl_hwnds=" (hws.Length = 5 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/WinGetControls.htm](../../docs-v2/docs/lib/WinGetControls.htm)

````ahk
for n, ctrl in WinGetControls("A")
{
    Result := MsgBox("Control #" n " is '" ctrl "'. Continue?",, 4)
    if (Result = "No")
        break
}
````
