# DateAdd

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_datetime.ahk:4](../../tests/doccheck/assert_datetime.ahk#L4)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Adds or subtracts time from a date-time value.

## Syntax

````text
Result := DateAdd(DateTime, Time, TimeUnits)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
#Requires AutoHotkey v2.0

MsgBox "DateAdd_days=" DateAdd("20240101", 3, "days")
MsgBox "DateAdd_hours=" DateAdd("20240101000000", 1, "hours")
MsgBox "DateAdd_minutes=" DateAdd("20240101000000", 90, "minutes")
MsgBox "DateAdd_seconds=" DateAdd("20240101000000", 30, "seconds")
````

## Upstream reference example

Source: [docs-v2/docs/lib/DateAdd.htm](../../docs-v2/docs/lib/DateAdd.htm)

````ahk
later := DateAdd(A_Now, 31, "days")
MsgBox FormatTime(later)
````
