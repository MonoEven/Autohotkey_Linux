# ObjOwnProps

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:142](../../tests/doccheck/assert_misc_cov.ahk#L142)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    o := {a: 1, b: 2, c: 3}
    n := 0
    for k in ObjOwnProps(o)
        n++
    return n
}
````
