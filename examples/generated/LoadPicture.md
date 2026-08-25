# LoadPicture

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_image.ahk:67](../../tests/doccheck/assert_image.ahk#L67)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Loads a picture from file and returns a bitmap or icon handle.

## Syntax

````text
Handle := LoadPicture(Filename , Options, &OutImageType)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk

; --- LoadPicture ---
hp := LoadPicture(IMGDIR "/red2.ppm")
Log("lp_ok=" (hp > 0 ? 1 : 0))
Log("lp_missing=" (LoadPicture(IMGDIR "/nope.ppm") = 0 ? 1 : 0))
Log("lp_resize=" (LoadPicture(IMGDIR "/red2.ppm", "w48 h-1") > 0 ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/LoadPicture.htm](../../docs-v2/docs/lib/LoadPicture.htm)

````ahk
Pics := []
; Find some pictures to display.
Loop Files, A_WinDir "\Web\Wallpaper\*.jpg", "R"
{
    ; Load each picture and add it to the array.
    Pics.Push(LoadPicture(A_LoopFileFullPath))
}
if !Pics.Length
{
    ; If this happens, edit the path on the Loop line above.
    MsgB
````
