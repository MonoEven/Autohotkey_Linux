# OnError

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:279](../../tests/doccheck/assert_misc_cov.ahk#L279)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Registers a function to be called automatically whenever an unhandled error occurs.

## Syntax

````text
OnError Callback , AddRemove
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    ExitApp 0
}
Check("onerror_reg", () => (OnError(OnErr), 1))
Provoke() {
    throw "misc-cov-probe-boom"
}
````

## Upstream reference example

Source: [docs-v2/docs/lib/OnError.htm](../../docs-v2/docs/lib/OnError.htm)

````ahk
OnError LogError
i := Integer("cause_error")
LogError(exception, mode) {
    FileAppend "Error on line " exception.Line ": " exception.Message "`n"
        , "errorlog.txt"
    return true
}
````
