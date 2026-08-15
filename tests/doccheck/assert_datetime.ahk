; Date/Time module doc-check (v2 docs: DateAdd/DateDiff/FormatTime/A_* vars).
#Requires AutoHotkey v2.0

MsgBox "DateAdd_days=" DateAdd("20240101", 3, "days")
MsgBox "DateAdd_hours=" DateAdd("20240101000000", 1, "hours")
MsgBox "DateAdd_minutes=" DateAdd("20240101000000", 90, "minutes")
MsgBox "DateAdd_seconds=" DateAdd("20240101000000", 30, "seconds")
; Docs: TimeUnits is one of Seconds/Minutes/Hours/Days (or just the first
; letter).  "months" prefix-matches Minutes; "years" is invalid and throws.
MsgBox "DateAdd_months=" DateAdd("20240101", 2, "months")
try
    DateAdd("20240101", 1, "years")
catch
    MsgBox "DateAdd_years_err=1"
MsgBox "DateDiff_forward=" DateDiff("20240104", "20240101", "days")
MsgBox "DateDiff_reverse=" DateDiff("20240101", "20240104", "days")
MsgBox "DateDiff_hours=" DateDiff("20240101120000", "20240101100000", "hours")
MsgBox "DateDiff_seconds=" DateDiff("20240101120030", "20240101120000", "seconds")

MsgBox "FormatTime_custom=" FormatTime("20240101120000", "yyyy-MM-dd HH:mm:ss")
MsgBox "FormatTime_12h=" FormatTime("20240101120000", "h:mm tt")
MsgBox "FormatTime_weekday=" FormatTime("20240101120000", "dddd")
MsgBox "FormatTime_month=" FormatTime("20240101120000", "MMMM")
MsgBox "FormatTime_short=" FormatTime("20240101", "ShortDate")
MsgBox "FormatTime_ymd=" FormatTime("20240101120000", "y")   ; 2-digit year
MsgBox "FormatTime_yweek=" FormatTime("20240101", "YWeek")
MsgBox "FormatTime_yday=" FormatTime("20240101", "YDay")
; Docs: blank Format produces "time followed by the long date" (locale dependent).
MsgBox "FormatTime_default_len=" (StrLen(FormatTime()) > 10)

; A_* variables
MsgBox "A_Now_len=" StrLen(A_Now)
MsgBox "A_NowUTC_len=" StrLen(A_NowUTC)
MsgBox "A_YYYY_num=" (A_YYYY > 2000)
MsgBox "A_MM_range=" (A_MM >= 1 && A_MM <= 12)
MsgBox "A_DD_range=" (A_DD >= 1 && A_DD <= 31)
MsgBox "A_Hour_range=" (A_Hour >= 0 && A_Hour <= 23)
MsgBox "A_Min_range=" (A_Min >= 0 && A_Min <= 59)
MsgBox "A_Sec_range=" (A_Sec >= 0 && A_Sec <= 59)
MsgBox "A_MSec_range=" (A_MSec >= 0 && A_MSec <= 999)
MsgBox "A_WDay_range=" (A_WDay >= 1 && A_WDay <= 7)
MsgBox "A_YDay_range=" (A_YDay >= 1 && A_YDay <= 366)
MsgBox "A_MMM_len=" StrLen(A_MMM)
MsgBox "A_MMMM_len=" (StrLen(A_MMMM) > StrLen(A_MMM))
MsgBox "A_DDD_len=" StrLen(A_DDD)
MsgBox "A_NowUTC_differs_or_not=" (A_NowUTC != A_Now || A_NowUTC = A_Now)
