# RTrim

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_clipboard.ahk:32](../../tests/doccheck/assert_clipboard.ahk#L32)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_string.ahk:25](../../tests/doccheck/assert_string.ahk#L25)

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    }
    t := FileExist(TBASE ".txt") ? FileRead(TBASE ".txt") : ""
    return RTrim(t, "`r`n")   ; probe appends a trailing newline
}

; --- write side: AHK takes CLIPBOARD ownership ---
````
