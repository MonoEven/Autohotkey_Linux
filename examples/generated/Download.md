# Download

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:297](../../tests/doccheck/assert_sys.ahk#L297)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`
- Linux adaptation: Uses curl/wget; requires the external tool

Downloads a file from the Internet.

## Syntax

````text
Download URL, Filename
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

try {
    Download("http://127.0.0.1:18765/serve.txt", "/tmp/ahk_dc_sys/dl.txt")
    dl_ok := "noerr"
} catch OSError {
    dl_ok := "OSError"
````

## Upstream reference example

Source: [docs-v2/docs/lib/Download.htm](../../docs-v2/docs/lib/Download.htm)

````ahk
Download "https://www.autohotkey.com/download/2.0/version.txt", "C:\AutoHotkey Latest Version.txt"
````
