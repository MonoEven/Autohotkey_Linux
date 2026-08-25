# ObjFromPtrAddRef

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:129](../../tests/doccheck/assert_misc_cov.ahk#L129)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    ; ObjFromPtrAddRef increments the count; the wrapper owns that ref.
    b := Buffer(8, 0)
    o := ObjFromPtrAddRef(ObjPtr(b))
    return Type(o)
}
Check("objfromptraddref", ObjFromPtrAddRefFn)
````
