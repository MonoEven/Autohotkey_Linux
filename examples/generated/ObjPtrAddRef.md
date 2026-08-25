# ObjPtrAddRef

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:98](../../tests/doccheck/assert_misc_cov.ahk#L98)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ObjPtrAddRefFn() {
    b := Buffer(8, 0)
    p := ObjPtrAddRef(b)
    ObjRelease(p)  ; Balance the AddRef.
    return p != 0
}
````
