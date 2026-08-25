# ObjAddRef

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:105](../../tests/doccheck/assert_misc_cov.ahk#L105)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Increments or decrements an object's reference count.

## Syntax

````text
NewRefCount := ObjAddRef(Ptr) NewRefCount := ObjRelease(Ptr)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
ObjAddRefFn() {
    b := Buffer(8, 0)
    n := ObjAddRef(ObjPtr(b))
    ObjRelease(ObjPtr(b))  ; Balance.
    return n >= 1 ? "ok" : n
}
````

## Upstream reference example

Source: [docs-v2/docs/lib/ObjAddRef.htm](../../docs-v2/docs/lib/ObjAddRef.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
