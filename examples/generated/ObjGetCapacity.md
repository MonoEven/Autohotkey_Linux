# ObjGetCapacity

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:136](../../tests/doccheck/assert_misc_cov.ahk#L136)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    o := Object()
    ObjSetCapacity(o, 100)
    return ObjGetCapacity(o)
}
Check("objcap", ObjCap)
ObjProps() {
````
