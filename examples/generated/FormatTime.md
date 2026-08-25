# FormatTime

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_datetime.ahk:20](../../tests/doccheck/assert_datetime.ahk#L20)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Transforms a YYYYMMDDHH24MISS timestamp into the specified date/time format.

## Syntax

````text
String := FormatTime(YYYYMMDDHH24MISS, Format)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "DateDiff_seconds=" DateDiff("20240101120030", "20240101120000", "seconds")

MsgBox "FormatTime_custom=" FormatTime("20240101120000", "yyyy-MM-dd HH:mm:ss")
MsgBox "FormatTime_12h=" FormatTime("20240101120000", "h:mm tt")
MsgBox "FormatTime_weekday=" FormatTime("20240101120000", "dddd")
MsgBox "FormatTime_month=" FormatTime("20240101120000", "MMMM")
````

## Upstream reference example

Source: [docs-v2/docs/lib/FormatTime.htm](../../docs-v2/docs/lib/FormatTime.htm)

````ahk
TimeString := FormatTime()
MsgBox "The current time and date (time first) is " TimeString
TimeString := FormatTime("R")
MsgBox "The current time and date (date first) is " TimeString
TimeString := FormatTime(, "Time")
MsgBox "The current time is " TimeString
TimeString := FormatTime("T12
````
