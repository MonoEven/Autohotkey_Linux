# FileAppend

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:16](../../tests/doccheck/assert_clipboard.ahk#L16)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_clipboard_change.ahk:21](../../tests/doccheck/assert_clipboard_change.ahk#L21)
- `x11`: [tests/doccheck/assert_clipboard_slow.ahk:13](../../tests/doccheck/assert_clipboard_slow.ahk#L13)
- `x11`: [tests/doccheck/assert_ctrl.ahk:9](../../tests/doccheck/assert_ctrl.ahk#L9)
- `x11`: [tests/doccheck/assert_dialog.ahk:11](../../tests/doccheck/assert_dialog.ahk#L11)
- `x11`: [tests/doccheck/assert_edit.ahk:25](../../tests/doccheck/assert_edit.ahk#L25)
- `headless`: [tests/doccheck/assert_file.ahk:16](../../tests/doccheck/assert_file.ahk#L16)
- `x11`: [tests/doccheck/assert_gui.ahk:9](../../tests/doccheck/assert_gui.ahk#L9)
- `x11`: [tests/doccheck/assert_hotkey.ahk:9](../../tests/doccheck/assert_hotkey.ahk#L9)
- `x11`: [tests/doccheck/assert_hotkey_btn.ahk:17](../../tests/doccheck/assert_hotkey_btn.ahk#L17)
- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:15](../../tests/doccheck/assert_hotkey_lr.ahk#L15)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:20](../../tests/doccheck/assert_hotkey_pt.ahk#L20)
- `x11`: [tests/doccheck/assert_hotstring.ahk:10](../../tests/doccheck/assert_hotstring.ahk#L10)
- `x11`: [tests/doccheck/assert_image.ahk:22](../../tests/doccheck/assert_image.ahk#L22)
- `desktop-session`: [tests/doccheck/assert_ime.ahk:17](../../tests/doccheck/assert_ime.ahk#L17)
- `x11`: [tests/doccheck/assert_input.ahk:10](../../tests/doccheck/assert_input.ahk#L10)
- `x11`: [tests/doccheck/assert_inputhook.ahk:11](../../tests/doccheck/assert_inputhook.ahk#L11)
- `x11`: [tests/doccheck/assert_layout.ahk:9](../../tests/doccheck/assert_layout.ahk#L9)
- `x11`: [tests/doccheck/assert_monitor.ahk:10](../../tests/doccheck/assert_monitor.ahk#L10)
- `x11`: [tests/doccheck/assert_msg.ahk:23](../../tests/doccheck/assert_msg.ahk#L23)
- `headless`: [tests/doccheck/assert_notimpl.ahk:8](../../tests/doccheck/assert_notimpl.ahk#L8)
- `x11`: [tests/doccheck/assert_repeat.ahk:15](../../tests/doccheck/assert_repeat.ahk#L15)
- `x11`: [tests/doccheck/assert_shape.ahk:19](../../tests/doccheck/assert_shape.ahk#L19)
- `host-tools`: [tests/doccheck/assert_sound_etc.ahk:13](../../tests/doccheck/assert_sound_etc.ahk#L13)
- `headless`: [tests/doccheck/assert_statements.ahk:13](../../tests/doccheck/assert_statements.ahk#L13)
- `headless`: [tests/doccheck/assert_sys.ahk:140](../../tests/doccheck/assert_sys.ahk#L140)
- `x11`: [tests/doccheck/assert_timer.ahk:10](../../tests/doccheck/assert_timer.ahk#L10)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:11](../../tests/doccheck/assert_unicode_lease.ahk#L11)
- `wayland`: [tests/doccheck/assert_wayland.ahk:29](../../tests/doccheck/assert_wayland.ahk#L29)
- `x11`: [tests/doccheck/assert_win.ahk:10](../../tests/doccheck/assert_win.ahk#L10)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:31](../../tests/doccheck/assert_misc_cov.ahk#L31)

Writes text or binary data to the end of a file (first creating the file, if necessary).

## Syntax

````text
FileAppend Text , Filename, Options
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
OUT := "/tmp/ahk_dc_clip_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

PROBE := "out/xclip_probe"
TBASE := "/tmp/ahk_dc_probe"
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileAppend.htm](../../docs-v2/docs/lib/FileAppend.htm)

````ahk
FileAppend "Another line.`n", "C:\My Documents\Test.txt"
````
