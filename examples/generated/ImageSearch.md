# ImageSearch

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_image.ahk:60](../../tests/doccheck/assert_image.ahk#L60)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Searches a region of the screen for an image.

## Syntax

````text
ImageSearch &OutputVarX, &OutputVarY, X1, Y1, X2, Y2, ImageFile
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
wr1 := 0
loop 100 {
    if ImageSearch(&wr0, &wr1, 60, 80, 99, 109, IMGDIR "/red2.ppm")
        break
    Sleep(100)
}
````

## Upstream reference example

Source: [docs-v2/docs/lib/ImageSearch.htm](../../docs-v2/docs/lib/ImageSearch.htm)

````ahk
ImageSearch &FoundX, &FoundY, 40, 40, 300, 300, "C:\My Images\test.bmp"
````
