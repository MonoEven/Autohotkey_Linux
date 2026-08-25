# CallbackFree

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `host-tools`
- Verified source: [tests/doccheck/assert_sound_etc.ahk:44](../../tests/doccheck/assert_sound_etc.ahk#L44)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
cb := CallbackCreate((*) => 42, "C", 0)
Log("cbaddr=" (IsInteger(cb) && cb > 0 ? "ok" : "bad"))
CallbackFree(cb)
Log("cbfree=ok")

; --- InputHook state machine (no live capture on Linux) ----------------------
````
