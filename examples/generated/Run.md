# Run

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:73](../../tests/doccheck/assert_clipboard.ahk#L73)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_clipboard_change.ahk:34](../../tests/doccheck/assert_clipboard_change.ahk#L34)
- `headless`: [tests/doccheck/assert_clipboard_pasterestore.ahk:42](../../tests/doccheck/assert_clipboard_pasterestore.ahk#L42)
- `x11`: [tests/doccheck/assert_clipboard_slow.ahk:21](../../tests/doccheck/assert_clipboard_slow.ahk#L21)
- `x11`: [tests/doccheck/assert_ctrl.ahk:15](../../tests/doccheck/assert_ctrl.ahk#L15)
- `x11`: [tests/doccheck/assert_edit.ahk:30](../../tests/doccheck/assert_edit.ahk#L30)
- `x11`: [tests/doccheck/assert_hotkey_btn.ahk:51](../../tests/doccheck/assert_hotkey_btn.ahk#L51)
- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:53](../../tests/doccheck/assert_hotkey_lr.ahk#L53)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:53](../../tests/doccheck/assert_hotkey_pt.ahk#L53)
- `x11`: [tests/doccheck/assert_hotstring.ahk:19](../../tests/doccheck/assert_hotstring.ahk#L19)
- `x11`: [tests/doccheck/assert_image.ahk:44](../../tests/doccheck/assert_image.ahk#L44)
- `x11`: [tests/doccheck/assert_input.ahk:23](../../tests/doccheck/assert_input.ahk#L23)
- `x11`: [tests/doccheck/assert_inputhook.ahk:20](../../tests/doccheck/assert_inputhook.ahk#L20)
- `x11`: [tests/doccheck/assert_layout.ahk:16](../../tests/doccheck/assert_layout.ahk#L16)
- `x11`: [tests/doccheck/assert_monitor.ahk:12](../../tests/doccheck/assert_monitor.ahk#L12)
- `x11`: [tests/doccheck/assert_msg.ahk:25](../../tests/doccheck/assert_msg.ahk#L25)
- `x11`: [tests/doccheck/assert_repeat.ahk:23](../../tests/doccheck/assert_repeat.ahk#L23)
- `x11`: [tests/doccheck/assert_shape.ahk:21](../../tests/doccheck/assert_shape.ahk#L21)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:16](../../tests/doccheck/assert_unicode_lease.ahk#L16)
- `x11`: [tests/doccheck/assert_win.ahk:13](../../tests/doccheck/assert_win.ahk#L13)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:233](../../tests/doccheck/assert_misc_cov.ahk#L233)
- `x11`: [tests/doccheck/assert_clipboard_all.ahk:146](../../tests/doccheck/assert_clipboard_all.ahk#L146)

Runs an external program. Unlike Run, RunWait will wait until the program finishes before continuing.

## Syntax

````text
Run Target , WorkingDir, Options, &OutputVarPID ExitCode := RunWait(Target , WorkingDir, Options, &OutputVarPID)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
;     for 3 s; AHK must read the text while the foreign owner is alive ---
FileDelete(TBASE "_own.txt")
Run(PROBE ' --set --delay 3000 -out ' TBASE '_own.txt')
i := 0
while !FileExist(TBASE "_own.txt") && i < 40 {
    Sleep(50)
````

## Upstream reference example

Source: [docs-v2/docs/lib/Run.htm](../../docs-v2/docs/lib/Run.htm)

````ahk
Run "notepad"
````
