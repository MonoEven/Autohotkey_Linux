# Type

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `dbus`
- Verified source: [tests/doccheck/assert_com.ahk:57](../../tests/doccheck/assert_com.ahk#L57)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `x11`: [tests/doccheck/assert_dialog.ahk:24](../../tests/doccheck/assert_dialog.ahk#L24)
- `x11`: [tests/doccheck/assert_gui.ahk:94](../../tests/doccheck/assert_gui.ahk#L94)
- `desktop-session`: [tests/doccheck/assert_ime.ahk:30](../../tests/doccheck/assert_ime.ahk#L30)
- `headless`: [tests/doccheck/assert_math.ahk:44](../../tests/doccheck/assert_math.ahk#L44)
- `headless`: [tests/doccheck/assert_notimpl.ahk:13](../../tests/doccheck/assert_notimpl.ahk#L13)
- `headless`: [tests/doccheck/assert_object.ahk:12](../../tests/doccheck/assert_object.ahk#L12)
- `host-tools`: [tests/doccheck/assert_sound_etc.ahk:49](../../tests/doccheck/assert_sound_etc.ahk#L49)
- `headless`: [tests/doccheck/assert_sys.ahk:194](../../tests/doccheck/assert_sys.ahk#L194)
- `x11`: [tests/doccheck/assert_misc_cov.ahk:46](../../tests/doccheck/assert_misc_cov.ahk#L46)

Returns the class name of a value.

## Syntax

````text
ClassName := Type(Value)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    MsgBox "bogus_ok=1"
} catch as e {
    MsgBox "bogus_err=" (Type(e) = "OSError")
}

; --- ComObjGet alias ---
````

## Upstream reference example

Source: [docs-v2/docs/lib/Type.htm](../../docs-v2/docs/lib/Type.htm)

````ahk
a := 1, b := 2.0, c := "3"
MsgBox Type(a)  ; Integer
MsgBox Type(b)  ; Float
MsgBox Type(c)  ; String
````
