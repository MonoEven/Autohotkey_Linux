# WinWaitNotActive

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:255](../../tests/doccheck/assert_misc_cov.ahk#L255)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; WinWaitNotActive: nonexistent window is treated as already inactive.
WnaFn() {
    return WinWaitNotActive("No Such Window Ever",, 0.2)
}
Check("wna_none", WnaFn)
````
