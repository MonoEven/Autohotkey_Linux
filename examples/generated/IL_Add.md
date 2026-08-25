# IL_Add

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_image.ahk:135](../../tests/doccheck/assert_image.ahk#L135)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("il_destroy_again=" (IL_Destroy(h1) = 0 ? 1 : 0)) ; Already destroyed.
Log("il_destroy_invalid=" (IL_Destroy(99999999) = 0 ? 1 : 0))
Log("il_add1=" (IL_Add(h2, IMGDIR "/red2.ppm") = 1 ? 1 : 0))
Log("il_add2=" (IL_Add(h2, IMGDIR "/blue2.ppm") = 2 ? 1 : 0))
Log("il_add_missing=" (IL_Add(h2, IMGDIR "/nope.ppm") = 0 ? 1 : 0))
Log("il_add_dead=" (IL_Add(h1, IMGDIR "/red2.ppm") = 0 ? 1 : 0))
````
