# FileDelete

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:15](../../tests/doccheck/assert_clipboard.ahk#L15)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_clipboard_change.ahk:19](../../tests/doccheck/assert_clipboard_change.ahk#L19)
- `x11`: [tests/doccheck/assert_clipboard_slow.ahk:12](../../tests/doccheck/assert_clipboard_slow.ahk#L12)
- `x11`: [tests/doccheck/assert_ctrl.ahk:8](../../tests/doccheck/assert_ctrl.ahk#L8)
- `headless`: [tests/doccheck/assert_diag.ahk:8](../../tests/doccheck/assert_diag.ahk#L8)
- `x11`: [tests/doccheck/assert_dialog.ahk:10](../../tests/doccheck/assert_dialog.ahk#L10)
- `x11`: [tests/doccheck/assert_edit.ahk:24](../../tests/doccheck/assert_edit.ahk#L24)
- `headless`: [tests/doccheck/assert_file.ahk:20](../../tests/doccheck/assert_file.ahk#L20)
- `headless`: [tests/doccheck/assert_general.ahk:66](../../tests/doccheck/assert_general.ahk#L66)
- `x11`: [tests/doccheck/assert_gui.ahk:8](../../tests/doccheck/assert_gui.ahk#L8)
- `x11`: [tests/doccheck/assert_hotkey.ahk:8](../../tests/doccheck/assert_hotkey.ahk#L8)
- `x11`: [tests/doccheck/assert_hotkey_btn.ahk:16](../../tests/doccheck/assert_hotkey_btn.ahk#L16)
- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:14](../../tests/doccheck/assert_hotkey_lr.ahk#L14)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:19](../../tests/doccheck/assert_hotkey_pt.ahk#L19)
- `x11`: [tests/doccheck/assert_hotstring.ahk:9](../../tests/doccheck/assert_hotstring.ahk#L9)
- `x11`: [tests/doccheck/assert_image.ahk:21](../../tests/doccheck/assert_image.ahk#L21)
- `desktop-session`: [tests/doccheck/assert_ime.ahk:16](../../tests/doccheck/assert_ime.ahk#L16)
- `x11`: [tests/doccheck/assert_input.ahk:9](../../tests/doccheck/assert_input.ahk#L9)
- `x11`: [tests/doccheck/assert_inputhook.ahk:10](../../tests/doccheck/assert_inputhook.ahk#L10)
- `x11`: [tests/doccheck/assert_layout.ahk:8](../../tests/doccheck/assert_layout.ahk#L8)
- `x11`: [tests/doccheck/assert_monitor.ahk:9](../../tests/doccheck/assert_monitor.ahk#L9)
- `x11`: [tests/doccheck/assert_msg.ahk:22](../../tests/doccheck/assert_msg.ahk#L22)
- `headless`: [tests/doccheck/assert_notimpl.ahk:7](../../tests/doccheck/assert_notimpl.ahk#L7)
- `headless`: [tests/doccheck/assert_parity.ahk:10](../../tests/doccheck/assert_parity.ahk#L10)
- `x11`: [tests/doccheck/assert_repeat.ahk:14](../../tests/doccheck/assert_repeat.ahk#L14)
- `x11`: [tests/doccheck/assert_shape.ahk:18](../../tests/doccheck/assert_shape.ahk#L18)
- `host-tools`: [tests/doccheck/assert_sound_etc.ahk:12](../../tests/doccheck/assert_sound_etc.ahk#L12)
- `headless`: [tests/doccheck/assert_statements.ahk:12](../../tests/doccheck/assert_statements.ahk#L12)
- `x11`: [tests/doccheck/assert_timer.ahk:9](../../tests/doccheck/assert_timer.ahk#L9)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:10](../../tests/doccheck/assert_unicode_lease.ahk#L10)
- `wayland`: [tests/doccheck/assert_wayland.ahk:28](../../tests/doccheck/assert_wayland.ahk#L28)
- `x11`: [tests/doccheck/assert_win.ahk:9](../../tests/doccheck/assert_win.ahk#L9)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:30](../../tests/doccheck/assert_misc_cov.ahk#L30)
- `x11`: [tests/doccheck/assert_clipboard_all.ahk:32](../../tests/doccheck/assert_clipboard_all.ahk#L32)

Deletes one or more files permanently.

## Syntax

````text
FileDelete FilePattern
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

OUT := "/tmp/ahk_dc_clip_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

PROBE := "out/xclip_probe"
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileDelete.htm](../../docs-v2/docs/lib/FileDelete.htm)

````ahk
FileDelete "C:\temp files\*.tmp"
````
