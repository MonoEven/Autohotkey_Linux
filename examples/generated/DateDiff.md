# DateDiff

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_datetime.ahk:15](../../tests/doccheck/assert_datetime.ahk#L15)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Compares two date-time values and returns the difference.

## Syntax

````text
Result := DateDiff(DateTime1, DateTime2, TimeUnits)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
catch
    MsgBox "DateAdd_years_err=1"
MsgBox "DateDiff_forward=" DateDiff("20240104", "20240101", "days")
MsgBox "DateDiff_reverse=" DateDiff("20240101", "20240104", "days")
MsgBox "DateDiff_hours=" DateDiff("20240101120000", "20240101100000", "hours")
MsgBox "DateDiff_seconds=" DateDiff("20240101120030", "20240101120000", "seconds")
````

## Upstream reference example

Source: [docs-v2/docs/lib/DateDiff.htm](../../docs-v2/docs/lib/DateDiff.htm)

````ahk
var1 := "20050126"
var2 := "20040126"
MsgBox DateDiff(var1, var2, "days")  ; The answer will be 366 since 2004 is a leap year.
````
