# FileOpen

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_ctrl.ahk:25](../../tests/doccheck/assert_ctrl.ahk#L25)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Additional verified environments

- `headless`: [tests/doccheck/assert_file.ahk:24](../../tests/doccheck/assert_file.ahk#L24)
- `x11`: [tests/doccheck/assert_image.ahk:28](../../tests/doccheck/assert_image.ahk#L28)
- `x11`: [tests/doccheck/assert_input.ahk:30](../../tests/doccheck/assert_input.ahk#L30)
- `x11`: [tests/doccheck/assert_layout.ahk:23](../../tests/doccheck/assert_layout.ahk#L23)
- `x11`: [tests/doccheck/assert_repeat.ahk:30](../../tests/doccheck/assert_repeat.ahk#L30)

Opens a file to read specific content from it and/or to write new content into it.

## Syntax

````text
FileObj := FileOpen(Filename, Flags , Encoding)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
next_lines() {
    global prev_bytes
    f := FileOpen(EVOUT, "r")
    f.Seek(prev_bytes)
    rest := f.Read()
    f.Close()
````

## Upstream reference example

Source: [docs-v2/docs/lib/FileOpen.htm](../../docs-v2/docs/lib/FileOpen.htm)

````ahk
FileName := FileSelect("S16",, "Create a new file:")
if (FileName = "")
    return
try
    FileObj := FileOpen(FileName, "w")
catch as Err
{
    MsgBox "Can't open '" FileName "' for writing."
        . "`n`n" Type(Err) ": " Err.Message
    return
}
TestString := "This is a tes
````
