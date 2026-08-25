# ComObjFlags

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `dbus`
- Verified source: [tests/doccheck/assert_com.ahk:76](../../tests/doccheck/assert_com.ahk#L76)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: COM is a D-Bus adaptation: flags operate on the D-Bus wrapper, not a COM object

Retrieves or changes flags which control a COM wrapper object's behaviour.

## Syntax

````text
Flags := ComObjFlags(ComObj , NewFlags, Mask)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- ComObjFlags ---
vf2 := ComValue(3, 1)
MsgBox "flags0=" (ComObjFlags(vf2) = 0)
ComObjFlags(vf2, 1)
MsgBox "flags1=" (ComObjFlags(vf2) = 1)
````

## Upstream reference example

Source: [docs-v2/docs/lib/ComObjFlags.htm](../../docs-v2/docs/lib/ComObjFlags.htm)

````ahk
arr := ComObjArray(0xC, 1)
if ComObjFlags(arr) & 1
    MsgBox "arr will be automatically destroyed."
else
    MsgBox "arr will not be automatically destroyed."
````
