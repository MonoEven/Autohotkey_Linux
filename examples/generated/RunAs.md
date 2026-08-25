# RunAs

- Linux status: `IMPL` (P4)
- Example kind: `verified-error`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_msg.ahk:214](../../tests/doccheck/assert_msg.ahk#L214)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: No logon API: Run/RunWait raise "Launch Error (possibly related to RunAs)"

Specifies a set of user credentials to use for all subsequent Run and RunWait functions.

## Syntax

````text
RunAs User, Password, Domain
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; --- RunAs: stores credentials (docs); launching with credentials is not
; possible on Linux (no logon API), so Run then raises an error ---
RunAs("user", "pass") ; Set credentials.
Log("ra_set=1")
try {
    Run("true")
````

## Upstream reference example

Source: [docs-v2/docs/lib/RunAs.htm](../../docs-v2/docs/lib/RunAs.htm)

````ahk
RunAs "Administrator", "MyPassword"
Run "RegEdit.exe"
RunAs  ; Reset to normal behavior.
````
