# VerCompare

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_general.ahk:107](../../tests/doccheck/assert_general.ahk#L107)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Compares two version strings.

## Syntax

````text
Result := VerCompare(VersionA, VersionB)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; Sort / VerCompare / Type already covered elsewhere; spot checks:
MsgBox "VerCompare=" VerCompare("2.0.26", "2.0.20")
MsgBox "GetKeyName=" GetKeyName("Enter")
MsgBox "IsLabel_yes=" IsLabel("dc_label")
MsgBox "IsLabel_no=" IsLabel("nope")
````

## Upstream reference example

Source: [docs-v2/docs/lib/VerCompare.htm](../../docs-v2/docs/lib/VerCompare.htm)

````ahk
if VerCompare(A_AhkVersion, "2.0") < 0
    MsgBox "This version < 2.0; possibly a pre-release version."
else
    MsgBox "This version is 2.0 or later."
````
