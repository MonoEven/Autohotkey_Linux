# GetKeySC

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:197](../../tests/doccheck/assert_misc_cov.ahk#L197)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves the scan code of a key.

## Syntax

````text
SC := GetKeySC(KeyName)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- Key lookup (headless-safe: static keysym tables) --------------------
Check("getkeyvk", () => GetKeyVK("a") != 0)
Check("getkeysc", () => (GetKeySC("b") = 0x30 && GetKeySC("F13") = 0x64
    && GetKeySC("RCtrl") = 0x11D && GetKeySC("Delete") = 0x153
    && GetKeySC("Pause") = 0x45 && GetKeySC("NumLock") = 0x145) ? 1 : 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/GetKeySC.htm](../../docs-v2/docs/lib/GetKeySC.htm)

````ahk
sc_code := GetKeySC("LControl")
MsgBox Format("sc{:X}", sc_code) ; Reports sc1D
````
