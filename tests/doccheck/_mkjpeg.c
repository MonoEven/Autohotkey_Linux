/* _mkjpeg.c: generate the JPEG doc-check fixture with libjpeg (one-off).
 * Writes fixtures/test.jpg = 8x8 solid red.  A flat DC-only image
 * round-trips through libjpeg to exactly 0xFF0000, so an exact ImageSearch
 * can find it.  Compile: gcc _mkjpeg.c -o _mkjpeg -ljpeg */
#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <string.h>

static int write_jpeg(const char *path, int w, int h, unsigned char r,
                      unsigned char g, unsigned char b)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    cinfo.err = jpeg_std_error(&jerr); // Required before jpeg_create_compress.
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, f);
    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, 95, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    unsigned char *row = malloc((size_t)w * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) { row[x * 3] = r; row[x * 3 + 1] = g; row[x * 3 + 2] = b; }
    while (cinfo.next_scanline < (JDIMENSION)h)
    {
        JSAMPROW prow = row;
        jpeg_write_scanlines(&cinfo, &prow, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    free(row);
    fclose(f);
    return 0;
}

int main(void)
{
    const char *base = "/mnt/f/AI/Codex/Autohotkey_Linux/tests/doccheck/fixtures";
    char p[512];
    snprintf(p, sizeof(p), "%s/test.jpg", base);
    if (write_jpeg(p, 8, 8, 255, 0, 0)) { fprintf(stderr, "write failed\n"); return 1; }
    long sz = -1;
    FILE *f = fopen(p, "rb"); if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
    fprintf(stderr, "wrote %s (%ld bytes)\n", p, sz);
    return 0;
}
