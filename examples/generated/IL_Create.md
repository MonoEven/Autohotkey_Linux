# IL_Create

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_image.ahk:129](../../tests/doccheck/assert_image.ahk#L129)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- IL_Create / IL_Add / IL_Destroy ---
h1 := IL_Create()
h2 := IL_Create(4, 10, true)
Log("il_create=" (h1 > 0 && h2 > 0 && h1 != h2 ? 1 : 0))
Log("il_destroy_ok=" (IL_Destroy(h1) = 1 ? 1 : 0))
````
