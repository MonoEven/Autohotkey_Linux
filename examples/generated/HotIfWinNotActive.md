# HotIfWinNotActive

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:214](../../tests/doccheck/assert_misc_cov.ahk#L214)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("hotif_wactive", () => (HotIfWinActive("misc-cov-none"), HotIf(), 1))
Check("hotif_wexist", () => (HotIfWinExist("misc-cov-none"), HotIf(), 1))
Check("hotif_wnactive", () => (HotIfWinNotActive("misc-cov-none"), HotIf(), 1))
Check("hotif_wnexist", () => (HotIfWinNotExist("misc-cov-none"), HotIf(), 1))

; --- Clipboard -----------------------------------------------------------
````
