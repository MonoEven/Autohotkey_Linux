// Reports the SHAPE bounding region of a window (WinSetRegion test probe):
//   xshape_probe <window-id> <outfile>
// Prints "shape=N" plus "rect x y w h" lines (XShapeGetRectangles).
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	if (argc < 3)
		return 2;
	Display *d = XOpenDisplay(NULL);
	if (!d)
		return 2;
	Window w = (Window)strtoull(argv[1], NULL, 10);
	FILE *f = fopen(argv[2], "w");
	if (!f)
	{
		XCloseDisplay(d);
		return 2;
	}
	int ev = 0, err = 0;
	if (!XShapeQueryExtension(d, &ev, &err))
	{
		fprintf(f, "shape=unsupported\n");
		fclose(f);
		XCloseDisplay(d);
		return 0;
	}
	// "shaped=1" when the window has an explicit bounding shape (restore
	// removes it, so the window uses its default rectangular shape).
	Bool b_shaped = False, c_shaped = False;
	int bx = 0, by = 0, bw = 0, bh = 0, cx = 0, cy = 0, cw = 0, ch = 0;
	XShapeQueryExtents(d, w, &b_shaped, &bx, &by, &bw, &bh, &c_shaped, &cx, &cy, &cw, &ch);
	fprintf(f, "shaped=%d\n", b_shaped ? 1 : 0);
	int count = 0, ordering = 0;
	XRectangle *r = XShapeGetRectangles(d, w, ShapeBounding, &count, &ordering);
	fprintf(f, "shape=%d\n", count);
	for (int i = 0; i < count; ++i)
		fprintf(f, "rect %d %d %u %u\n", r[i].x, r[i].y, r[i].width, r[i].height);
	if (r)
		XFree(r);
	fclose(f);
	XCloseDisplay(d);
	return 0;
}
