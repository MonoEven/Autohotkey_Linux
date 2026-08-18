; LoadPicture/IL_*/ImageSearch doc-check (v2 docs).  Runs under Xvfb
; (run_check.sh --xvfb) with xwin_helper painting a solid red rectangle
; (-fill) on the main window; test images are generated as ASCII PPM files.
;
; Linux semantics (documented in CHECK_REPORT):
;   - LoadPicture decodes BMP (24/32-bit), ICO (DIB-embedded entries), PNG
;     (non-interlaced) and PPM (P6/P3), applies Wn/Hn
;     (nearest-neighbour, -1 = keep aspect, 0 = original), accepts
;     Icon/GDI+ options (icons are loaded as bitmaps), returns an opaque
;     handle into the port's image store, 0 on any error (docs).
;   - IL_Create/IL_Add/IL_Destroy keep image lists in the same store
;     (handles are stable ids; IL_Add returns the 1-based index or 0 on
;     failure; IL_Destroy returns 1/0; IL_Add(0, ...) raises ValueError).
;   - ImageSearch grabs the region with XGetImage (CoordMode Pixel aware),
;     supports exact and *N variation matching plus *wN/*hN/*IconN/*Trans;
;     returns 1 with the coordinates, 0 with blank outputs; ValueError on
;     bad options/unloadable image; OSError when the region is off-screen.
#Requires AutoHotkey v2.0

OUT := "/tmp/ahk_dc_image_out.txt"
FileDelete(OUT)
Log(line) => FileAppend(line "`n", OUT)

; --- Generate test images (ASCII PPM P3). ---
IMGDIR := "/tmp/ahk_dc_img"
DirCreate(IMGDIR)
MakePPM(file, w, h, r, g, b) {
    f := FileOpen(IMGDIR "/" file, "w")
    f.Write("P3`n" w " " h "`n255`n")
    loop h {
        row := ""
        loop w
            row .= r " " g " " b " "
        f.Write(row "`n")
    }
    f.Close()
}
MakePPM("red2.ppm", 2, 2, 255, 0, 0)
MakePPM("nearred2.ppm", 2, 2, 255, 3, 0)
MakePPM("blue2.ppm", 2, 2, 0, 0, 255)
MakePPM("checker3.ppm", 3, 3, 0, 0, 0)

; --- The window: a red rectangle at screen (60,80)-(99,109). ---
Run('out/xwin_helper -title ImgMain -class DocCheck -x 50 -y 60 -w 300 -h 200'
    ' -fill FF0000 10 20 40 30')
WinWait("ImgMain",, 5)
Sleep(300)
; The port defaults CoordMode Pixel to CLIENT (upstream default); every
; screen-region search below uses explicit screen coordinates, so pin the
; mode here before the first search (Xvfb is frameless and client coords
; coincide with screen coords, but sway/XWayland windows are offset).
CoordMode("Pixel", "Screen")
; Wait for the rectangle to be observable: sway/XWayland paints
; asynchronously (headless pixman renderer + wlr-screencopy can lag the
; window map), so a capture taken right after the map may still be blank.
; Poll with the exact-match search until it succeeds (bounded).
wr0 := 0
wr1 := 0
loop 100 {
    if ImageSearch(&wr0, &wr1, 60, 80, 99, 109, IMGDIR "/red2.ppm")
        break
    Sleep(100)
}
Log("wait_red_found=" (wr0 = 60 && wr1 = 80 ? 1 : 0))

; --- LoadPicture ---
hp := LoadPicture(IMGDIR "/red2.ppm")
Log("lp_ok=" (hp > 0 ? 1 : 0))
Log("lp_missing=" (LoadPicture(IMGDIR "/nope.ppm") = 0 ? 1 : 0))
Log("lp_resize=" (LoadPicture(IMGDIR "/red2.ppm", "w48 h-1") > 0 ? 1 : 0))
ot := ""
Log("lp_icon=" (LoadPicture(IMGDIR "/red2.ppm", "Icon1", &ot) > 0 && ot = "Icon" ? 1 : 0))
ot2 := ""
Log("lp_bmp_type=" (LoadPicture(IMGDIR "/red2.ppm",, &ot2) > 0 && ot2 = "Bitmap" ? 1 : 0))

; --- ICO decoding (DIB-embedded entries; transparent pixels are returned
; as the magenta sentinel 0xFFFF00FF - the port's image model has no alpha). ---
ico := A_ScriptDir "/fixtures/test.ico"
Log("ico_load=" (LoadPicture(ico) > 0 ? 1 : 0))
ot3 := ""
Log("ico_type=" (LoadPicture(ico, "w0 h0", &ot3) > 0 && ot3 = "Icon" ? 1 : 0))
Log("ico_resize=" (LoadPicture(ico, "w32 h32") > 0 ? 1 : 0))

; --- PNG decoding (RGBA, filter reconstruction via zlib; alpha -> magenta
; sentinel; non-interlaced only). ---
png := A_ScriptDir "/fixtures/test.png"
Log("png_load=" (LoadPicture(png) > 0 ? 1 : 0))
ot4 := ""
Log("png_type=" (LoadPicture(png, "w0 h0", &ot4) > 0 && ot4 = "Bitmap" ? 1 : 0))
Log("png_resize=" (LoadPicture(png, "w32 h32") > 0 ? 1 : 0))

; --- GIF decoding (first frame; hand-written LZW; transparency -> magenta
; sentinel like PNG/ICO). ---
gif := A_ScriptDir "/fixtures/test.gif"
Log("gif_load=" (LoadPicture(gif) > 0 ? 1 : 0))
ot5 := ""
Log("gif_type=" (LoadPicture(gif, "w0 h0", &ot5) > 0 && ot5 = "Bitmap" ? 1 : 0))
Log("gif_resize=" (LoadPicture(gif, "w16 h16") > 0 ? 1 : 0))
; Content-level check: a solid-red 2x2 GIF (same shape as red2.ppm) must be
; found by ImageSearch in the red rectangle area.
gx := 0
gy := 0
gr := A_ScriptDir "/fixtures/test_red.gif"
Log("gif_red_search=" (ImageSearch(&gx, &gy, 60, 80, 99, 109, gr) = 1 ? 1 : 0))

; --- CUR (Windows cursor) decoding (ICONDIR type 2; DIB shared with ICO;
; the hotspot is not representable, documented). ---
cur := A_ScriptDir "/fixtures/test.cur"
Log("cur_load=" (LoadPicture(cur) > 0 ? 1 : 0))
ot6 := ""
Log("cur_type=" (LoadPicture(cur, "w0 h0", &ot6) > 0 && ot6 = "Icon" ? 1 : 0))
Log("cur_resize=" (LoadPicture(cur, "w32 h32") > 0 ? 1 : 0))

; --- JPEG decoding (via libjpeg; YUV->RGB by the library; no alpha). ---
jpg := A_ScriptDir "/fixtures/test.jpg"
Log("jpg_load=" (LoadPicture(jpg) > 0 ? 1 : 0))
ot7 := ""
Log("jpg_type=" (LoadPicture(jpg, "w0 h0", &ot7) > 0 && ot7 = "Bitmap" ? 1 : 0))
Log("jpg_resize=" (LoadPicture(jpg, "w32 h32") > 0 ? 1 : 0))
; Content check: an 8x8 solid-red JPEG.  JPEG is mathematically lossy (the
; integer YCbCr->RGB round-trip lands at 0xFE0000, not 0xFF0000), so use
; the documented *N per-channel variation, as ImageSearch users do for
; JPEG.
jx := 0
jy := 0
Log("jpg_red_search=" (ImageSearch(&jx, &jy, 60, 80, 99, 109, "*8 " jpg) = 1 ? 1 : 0))

; --- IL_Create / IL_Add / IL_Destroy ---
h1 := IL_Create()
h2 := IL_Create(4, 10, true)
Log("il_create=" (h1 > 0 && h2 > 0 && h1 != h2 ? 1 : 0))
Log("il_destroy_ok=" (IL_Destroy(h1) = 1 ? 1 : 0))
Log("il_destroy_again=" (IL_Destroy(h1) = 0 ? 1 : 0)) ; Already destroyed.
Log("il_destroy_invalid=" (IL_Destroy(99999999) = 0 ? 1 : 0))
Log("il_add1=" (IL_Add(h2, IMGDIR "/red2.ppm") = 1 ? 1 : 0))
Log("il_add2=" (IL_Add(h2, IMGDIR "/blue2.ppm") = 2 ? 1 : 0))
Log("il_add_missing=" (IL_Add(h2, IMGDIR "/nope.ppm") = 0 ? 1 : 0))
Log("il_add_dead=" (IL_Add(h1, IMGDIR "/red2.ppm") = 0 ? 1 : 0))
Log("il_add_resize=" (IL_Add(h2, IMGDIR "/red2.ppm", 0, true) = 3 ? 1 : 0))
try {
    IL_Add(0, IMGDIR "/red2.ppm")
    Log("il_add_zero=0")
} catch ValueError {
    Log("il_add_zero=1")
}

; --- ImageSearch ---
found := 0
Log("is_red=" (ImageSearch(&found, &fy, 60, 80, 99, 109, IMGDIR "/red2.ppm") = 1 ? 1 : 0))
Log("is_red_xy=" (found = 60 && fy = 80 ? 1 : 0))
bx := "x"
by := "x"
Log("is_blue=" (ImageSearch(&bx, &by, 60, 80, 99, 109, IMGDIR "/blue2.ppm") = 0 ? 1 : 0))
Log("is_blue_blank=" (bx = "" && by = "" ? 1 : 0))
nx := "x"
ny := "x"
; 0xFF0300 vs 0xFF0000: exact search must fail...
Log("is_near_exact=" (ImageSearch(&nx, &ny, 60, 80, 99, 109, IMGDIR "/nearred2.ppm") = 0 ? 1 : 0))
; ...but "*10" (allowed per-channel variation, docs) must succeed.
vx := "x"
vy := "x"
Log("is_near_var=" (ImageSearch(&vx, &vy, 60, 80, 99, 109, "*10 " IMGDIR "/nearred2.ppm") = 1 ? 1 : 0))
Log("is_near_var_xy=" (vx = 60 && vy = 80 ? 1 : 0))
; A search region that is entirely outside the window (the Xvfb root
; background, which is not red) finds nothing.
wx := "x"
wy := "x"
Log("is_nowhite=" (ImageSearch(&wx, &wy, 0, 0, 30, 30, IMGDIR "/red2.ppm") = 0 ? 1 : 0))
; ValueError: malformed option (docs: "invalid parameter was detected").
try {
    ImageSearch(&x1, &y1, 0, 0, 10, 10, "*z")
    Log("is_badopt=0")
} catch ValueError {
    Log("is_badopt=1")
}
; ValueError: image could not be loaded (docs).
try {
    ImageSearch(&x2, &y2, 0, 0, 10, 10, IMGDIR "/nope.ppm")
    Log("is_badfile=0")
} catch ValueError {
    Log("is_badfile=1")
}
; OSError: the region extends beyond the screen (docs: "internal function
; call fails").
try {
    ImageSearch(&x3, &y3, 0, 0, 2000, 2000, IMGDIR "/red2.ppm")
    Log("is_oserr=0")
} catch OSError {
    Log("is_oserr=1")
}

; --- Cleanup. ---
ExitApp(0)
