# ObjSetBase

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:150](../../tests/doccheck/assert_misc_cov.ahk#L150)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    base := {x: 10}
    o := {y: 5}
    ObjSetBase(o, base)
    return o.x
}
Check("objsetbase", ObjBase)
````
