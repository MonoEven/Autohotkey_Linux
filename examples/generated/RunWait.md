# RunWait

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:25](../../tests/doccheck/assert_clipboard.ahk#L25)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_clipboard_slow.ahk:52](../../tests/doccheck/assert_clipboard_slow.ahk#L52)
- `headless`: [tests/doccheck/assert_diag.ahk:13](../../tests/doccheck/assert_diag.ahk#L13)
- `headless`: [tests/doccheck/assert_general.ahk:52](../../tests/doccheck/assert_general.ahk#L52)
- `x11`: [tests/doccheck/assert_hotkey_btn.ahk:95](../../tests/doccheck/assert_hotkey_btn.ahk#L95)
- `x11`: [tests/doccheck/assert_hotkey_lr.ahk:89](../../tests/doccheck/assert_hotkey_lr.ahk#L89)
- `x11`: [tests/doccheck/assert_hotkey_pt.ahk:110](../../tests/doccheck/assert_hotkey_pt.ahk#L110)
- `x11`: [tests/doccheck/assert_hotstring.ahk:108](../../tests/doccheck/assert_hotstring.ahk#L108)
- `x11`: [tests/doccheck/assert_layout.ahk:41](../../tests/doccheck/assert_layout.ahk#L41)
- `headless`: [tests/doccheck/assert_parity.ahk:11](../../tests/doccheck/assert_parity.ahk#L11)
- `x11`: [tests/doccheck/assert_repeat.ahk:89](../../tests/doccheck/assert_repeat.ahk#L89)
- `x11`: [tests/doccheck/assert_shape.ahk:31](../../tests/doccheck/assert_shape.ahk#L31)
- `x11`: [tests/doccheck/assert_unicode_lease.ahk:60](../../tests/doccheck/assert_unicode_lease.ahk#L60)
- `wayland`: [tests/doccheck/assert_wayland.ahk:80](../../tests/doccheck/assert_wayland.ahk#L80)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:251](../../tests/doccheck/assert_misc_cov.ahk#L251)

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    global TBASE
    FileDelete(TBASE ".txt")
    RunWait(PROBE ' ' opts ' -out ' TBASE '.txt')
    i := 0
    while !FileExist(TBASE ".txt") && i < 60 {
        Sleep(50)
````
