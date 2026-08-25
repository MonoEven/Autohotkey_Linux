# SoundGetInterface

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `host-tools`
- Verified source: [tests/doccheck/assert_sound_etc.ahk:26](../../tests/doccheck/assert_sound_etc.ahk#L26)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Retrieves a native COM interface of a sound device or component.

## Syntax

````text
InterfacePtr := SoundGetInterface(IID, Component, Device)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- SoundGetInterface returns 0 (no COM on Linux) --------------------------
Log("sgi=" (IsInteger(SoundGetInterface("{00000000-0000-0000-C000-000000000046}")) ? "ok" : "bad"))
Log("sgi2=" (SoundGetInterface("{}") = 0 ? "ok" : "bad"))

; --- SoundGet*/Set* without a mixer tool -> OSError -------------------------
````

## Upstream reference example

Source: [docs-v2/docs/lib/SoundGetInterface.htm](../../docs-v2/docs/lib/SoundGetInterface.htm)

````ahk
; IAudioMeterInformation
audioMeter := SoundGetInterface("{C02216F6-8C67-4B5B-9D00-D008E73E0064}")
if audioMeter
{
    try loop  ; Until the script exits or an error occurs.
    {
        ; audioMeter->GetPeakValue(&peak)
        ComCall 3, audioMeter, "float*", &peak:=0
        Tool
````
