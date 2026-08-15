// X11 GUI implementation for the Linux port.
//
// Uses plain Xlib (no toolkit) so that dialogs work in any X11 environment
// (WSLg, XFCE, XWayland, ...).  When no display is available every function
// falls back to console I/O so scripts keep working headless.

#include "x11_gui.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <sys/select.h>
#include <unistd.h>

#ifndef MB_OKCANCEL
#define MB_OKCANCEL 0x00000001
#endif
#ifndef MB_ABORTRETRYIGNORE
#define MB_ABORTRETRYIGNORE 0x00000002
#endif
#ifndef MB_YESNOCANCEL
#define MB_YESNOCANCEL 0x00000003
#endif
#ifndef MB_RETRYCANCEL
#define MB_RETRYCANCEL 0x00000005
#endif
#ifndef MB_DEFBUTTON1
#define MB_DEFBUTTON1 0x00000000
#endif
#ifndef MB_DEFBUTTON2
#define MB_DEFBUTTON2 0x00000100
#endif
#ifndef MB_DEFBUTTON3
#define MB_DEFBUTTON3 0x00000200
#endif
#ifndef IDTIMEOUT
#define IDTIMEOUT 32000
#endif

// ---------------------------------------------------------------------------
// Display availability
// ---------------------------------------------------------------------------

bool LinuxHasDisplay()
{
	static int s_cached = -1;
	if (s_cached == -1)
	{
		Display *dpy = XOpenDisplay(nullptr);
		s_cached = dpy ? 1 : 0;
		if (dpy)
			XCloseDisplay(dpy);
	}
	return s_cached == 1;
}

// ---------------------------------------------------------------------------
// Shared X11 dialog machinery
// ---------------------------------------------------------------------------

namespace
{

struct Button
{
	const wchar_t *label;
	int id;
	int x, y, w, h;
};

struct DialogContext
{
	Display *dpy;
	Window win;
	GC gc;
	XFontStruct *font;
	int font_height;
	int win_w, win_h;
	const wchar_t *title;
	const wchar_t *text; // Already wrapped into lines separated by '\n'.
	Button buttons[4];
	int button_count;
	int default_id; // ID of the default button (Enter).
};

bool WideToNarrow(const wchar_t *aIn, char *aOut, size_t aOutSize)
{
	if (!aIn)
	{
		aOut[0] = '\0';
		return true;
	}
	size_t n = wcstombs(aOut, aIn, aOutSize - 1);
	if (n == (size_t)-1)
	{
		aOut[0] = '\0';
		return false;
	}
	aOut[n] = '\0';
	return true;
}

// Wrap aText into lines of at most aMaxChars, separated by '\n'.
void WrapText(const wchar_t *aText, wchar_t *aOut, size_t aOutSize, int aMaxChars)
{
	if (!aText)
	{
		aOut[0] = '\0';
		return;
	}
	size_t o = 0;
	size_t col = 0;
	for (const wchar_t *p = aText; *p && o + 2 < aOutSize; ++p)
	{
		if (*p == L'\n')
		{
			aOut[o++] = L'\n';
			col = 0;
			continue;
		}
		if (col >= (size_t)aMaxChars)
		{
			aOut[o++] = L'\n';
			col = 0;
		}
		aOut[o++] = *p;
		++col;
	}
	aOut[o] = L'\0';
}

int CountLines(const wchar_t *aText)
{
	int n = 1;
	for (const wchar_t *p = aText; *p; ++p)
		if (*p == L'\n')
			++n;
	return n;
}

void DrawTextWrapped(DialogContext &ctx, int aX, int aY)
{
	wchar_t wline[512];
	char line[1024];
	const wchar_t *p = ctx.text;
	while (p && *p)
	{
		const wchar_t *eol = wcschr(p, L'\n');
		size_t len = eol ? (size_t)(eol - p) : wcslen(p);
		if (len >= sizeof(wline) / sizeof(wline[0]))
			len = sizeof(wline) / sizeof(wline[0]) - 1;
		wmemcpy(wline, p, len);
		wline[len] = L'\0';
		WideToNarrow(wline, line, sizeof(line));
		XDrawString(ctx.dpy, ctx.win, ctx.gc, aX, aY, line, (int)strlen(line));
		aY += ctx.font_height;
		if (!eol)
			break;
		p = eol + 1;
	}
}

void DrawButtons(DialogContext &ctx)
{
	for (int i = 0; i < ctx.button_count; ++i)
	{
		Button &b = ctx.buttons[i];
		if (b.id == ctx.default_id)
		{
			// Draw a heavier border for the default button.
			XSetLineAttributes(ctx.dpy, ctx.gc, 3, LineSolid, CapButt, JoinMiter);
			XDrawRectangle(ctx.dpy, ctx.win, ctx.gc, b.x, b.y, b.w, b.h);
			XSetLineAttributes(ctx.dpy, ctx.gc, 1, LineSolid, CapButt, JoinMiter);
		}
		else
			XDrawRectangle(ctx.dpy, ctx.win, ctx.gc, b.x, b.y, b.w, b.h);
		char narrow[64];
		WideToNarrow(b.label, narrow, sizeof(narrow));
		int tw = XTextWidth(ctx.font, narrow, (int)strlen(narrow));
		XDrawString(ctx.dpy, ctx.win, ctx.gc
			, b.x + (b.w - tw) / 2, b.y + b.h / 2 + ctx.font_height / 3
			, narrow, (int)strlen(narrow));
	}
}

int HitTestButton(DialogContext &ctx, int aX, int aY)
{
	for (int i = 0; i < ctx.button_count; ++i)
	{
		Button &b = ctx.buttons[i];
		if (aX >= b.x && aX < b.x + b.w && aY >= b.y && aY < b.y + b.h)
			return b.id;
	}
	return 0;
}

// Returns the button id to use for Escape / window close, or 0 if the dialog
// must not be dismissed that way (i.e. only an OK button exists).
int CancelButtonId(DialogContext &ctx)
{
	for (int i = 0; i < ctx.button_count; ++i)
		if (ctx.buttons[i].id == IDCANCEL || ctx.buttons[i].id == IDNO)
			return ctx.buttons[i].id;
	// With a single OK button, Escape/close are usually treated as OK.
	return ctx.button_count == 1 ? ctx.buttons[0].id : 0;
}

static long long LinuxNowMs()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int RunX11Dialog(DialogContext &ctx, double aTimeout)
{
	XEvent ev;
	// Deadline in wall-clock milliseconds (CLOCK_MONOTONIC, not clock() which
	// measures CPU time and never advances while we sleep).
	long long deadline_ms = 0;
	if (aTimeout > 0)
		deadline_ms = LinuxNowMs() + (long long)(aTimeout * 1000);

	XMapRaised(ctx.dpy, ctx.win);
	XFlush(ctx.dpy);

	for (;;)
	{
		if (XPending(ctx.dpy))
		{
			XNextEvent(ctx.dpy, &ev);
			switch (ev.type)
			{
			case Expose:
				if (ev.xexpose.count == 0)
					DrawButtons(ctx);
				break;
			case ButtonPress:
				{
					int id = HitTestButton(ctx, ev.xbutton.x, ev.xbutton.y);
					if (id)
						return id;
				}
				break;
			case KeyPress:
				{
					KeySym ks = XLookupKeysym(&ev.xkey, 0);
					if (ks == XK_Return || ks == XK_KP_Enter)
						return ctx.default_id;
					if (ks == XK_Escape)
					{
						int id = CancelButtonId(ctx);
						if (id)
							return id;
					}
				}
				break;
			case ClientMessage:
				{
					Atom wm_delete = XInternAtom(ctx.dpy, "WM_DELETE_WINDOW", False);
					if ((Atom)ev.xclient.data.l[0] == wm_delete)
					{
						int id = CancelButtonId(ctx);
						if (id)
							return id;
					}
				}
				break;
			default:
				break;
			}
		}
		else if (aTimeout > 0)
		{
			long long now_ms = LinuxNowMs();
			if (now_ms >= deadline_ms)
				return IDTIMEOUT;
			int fd = ConnectionNumber(ctx.dpy);
			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(fd, &fds);
			struct timeval tv;
			long long remain = deadline_ms - now_ms;
			tv.tv_sec = (long)(remain / 1000);
			tv.tv_usec = (long)((remain % 1000) * 1000);
			if (select(fd + 1, &fds, nullptr, nullptr, &tv) < 0)
				return IDTIMEOUT;
		}
		else
		{
			// Block for the next event.
			XNextEvent(ctx.dpy, &ev);
			// Re-dispatch simple cases handled above.
			switch (ev.type)
			{
			case Expose:
				if (ev.xexpose.count == 0)
					DrawButtons(ctx);
				break;
			case ButtonPress:
				{
					int id = HitTestButton(ctx, ev.xbutton.x, ev.xbutton.y);
					if (id)
						return id;
				}
				break;
			case KeyPress:
				{
					KeySym ks = XLookupKeysym(&ev.xkey, 0);
					if (ks == XK_Return || ks == XK_KP_Enter)
						return ctx.default_id;
					if (ks == XK_Escape)
					{
						int id = CancelButtonId(ctx);
						if (id)
							return id;
					}
				}
				break;
			case ClientMessage:
				{
					Atom wm_delete = XInternAtom(ctx.dpy, "WM_DELETE_WINDOW", False);
					if ((Atom)ev.xclient.data.l[0] == wm_delete)
					{
						int id = CancelButtonId(ctx);
						if (id)
							return id;
					}
				}
				break;
			default:
				break;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Message box
// ---------------------------------------------------------------------------

struct MsgButtonSpec
{
	const wchar_t *label;
	int id;
};

int ShowMessageBoxX11(const wchar_t *aText, const wchar_t *aTitle, UINT aType, double aTimeout)
{
	Display *dpy = XOpenDisplay(nullptr);
	if (!dpy)
		return 0;
	DialogContext ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.dpy = dpy;
	ctx.title = aTitle && *aTitle ? aTitle : L"AutoHotkey";
	ctx.text = aText;
	ctx.default_id = IDOK;

	// Determine buttons from the type mask.
	MsgButtonSpec specs[4];
	int nspec = 0;
	switch (aType & 0xF)
	{
	case MB_OKCANCEL:
		specs[0] = {L"OK", IDOK};
		specs[1] = {L"Cancel", IDCANCEL};
		nspec = 2;
		break;
	case MB_ABORTRETRYIGNORE:
		specs[0] = {L"Abort", IDABORT};
		specs[1] = {L"Retry", IDRETRY};
		specs[2] = {L"Ignore", IDIGNORE};
		nspec = 3;
		break;
	case MB_YESNOCANCEL:
		specs[0] = {L"Yes", IDYES};
		specs[1] = {L"No", IDNO};
		specs[2] = {L"Cancel", IDCANCEL};
		nspec = 3;
		break;
	case MB_YESNO:
		specs[0] = {L"Yes", IDYES};
		specs[1] = {L"No", IDNO};
		nspec = 2;
		break;
	case MB_RETRYCANCEL:
		specs[0] = {L"Retry", IDRETRY};
		specs[1] = {L"Cancel", IDCANCEL};
		nspec = 2;
		break;
	default: // MB_OK
		specs[0] = {L"OK", IDOK};
		nspec = 1;
		break;
	}

	// Default button (Enter) from MB_DEFBUTTON*.
	int def_button = (int)((aType & 0xF00) >> 8); // 1..4
	if (def_button < 1 || def_button > nspec)
		def_button = 1;
	ctx.default_id = specs[def_button - 1].id;

	// Window geometry.
	int margin = 12;
	int button_w = 84, button_h = 30, button_gap = 10;
	wchar_t wrapped[8192];
	int max_chars = 60;
	WrapText(aText, wrapped, sizeof(wrapped) / sizeof(wrapped[0]), max_chars);
	ctx.text = wrapped;
	int line_count = CountLines(wrapped);
	if (line_count > 24)
		line_count = 24;
	ctx.win_w = 440;
	ctx.win_h = margin + line_count * 16 + 40 + button_h + margin;
	int button_area_w = nspec * button_w + (nspec - 1) * button_gap;
	int button_y = ctx.win_h - margin - button_h;
	int button_x = ctx.win_w - margin - button_area_w;
	for (int i = 0; i < nspec; ++i)
	{
		ctx.buttons[i].label = specs[i].label;
		ctx.buttons[i].id = specs[i].id;
		ctx.buttons[i].x = button_x;
		ctx.buttons[i].y = button_y;
		ctx.buttons[i].w = button_w;
		ctx.buttons[i].h = button_h;
		button_x += button_w + button_gap;
	}
	ctx.button_count = nspec;

	// Create the window.
	int scr = DefaultScreen(dpy);
	ctx.win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr)
		, 100, 100, ctx.win_w, ctx.win_h, 1
		, BlackPixel(dpy, scr), WhitePixel(dpy, scr));
	XStoreName(dpy, ctx.win, "AutoHotkey");
	Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, ctx.win, &wm_delete, 1);
	XSelectInput(dpy, ctx.win, ExposureMask | ButtonPressMask | KeyPressMask | StructureNotifyMask);
	ctx.gc = XCreateGC(dpy, ctx.win, 0, nullptr);
	ctx.font = XLoadQueryFont(dpy, "fixed");
	if (!ctx.font)
		ctx.font = XLoadQueryFont(dpy, "9x15");
	if (!ctx.font)
	{
		XCloseDisplay(dpy);
		return 0;
	}
	XSetFont(dpy, ctx.gc, ctx.font->fid);
	ctx.font_height = ctx.font->ascent + ctx.font->descent;

	// Set WM hints so window managers show a proper title.
	XSizeHints hints;
	memset(&hints, 0, sizeof(hints));
	hints.flags = PSize | PMinSize | PMaxSize;
	hints.width = hints.min_width = hints.max_width = ctx.win_w;
	hints.height = hints.min_height = hints.max_height = ctx.win_h;
	XSetWMNormalHints(dpy, ctx.win, &hints);
	char title_narrow[512];
	WideToNarrow(ctx.title, title_narrow, sizeof(title_narrow));
	XStoreName(dpy, ctx.win, title_narrow);

	// Pre-draw text so the first expose shows everything.
	XClearWindow(dpy, ctx.win);
	DrawTextWrapped(ctx, margin, margin + ctx.font->ascent);

	int result = RunX11Dialog(ctx, aTimeout);

	XFreeFont(dpy, ctx.font);
	XFreeGC(dpy, ctx.gc);
	XDestroyWindow(dpy, ctx.win);
	XCloseDisplay(dpy);
	return result;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// Set by Script::MsgBox() (window.cpp) so timed dialogs auto-dismiss.
double g_LinuxMsgBoxTimeout = 0;

// Sound the X11 bell (used by SoundBeep when a display is available).
void LinuxXBell()
{
	Display *dpy = XOpenDisplay(nullptr);
	if (dpy)
	{
		XBell(dpy, 0);
		XFlush(dpy);
		XCloseDisplay(dpy);
	}
}

int LinuxMessageBox(HWND, LPCTSTR aText, LPCTSTR aTitle, UINT aType, double aTimeout)
{
	const size_t msgbox_text_size = 8192;
	if (!LinuxHasDisplay())
	{
		// Headless fallback: print the message to the console.
		char narrow[msgbox_text_size];
		WideToNarrow(aText, narrow, sizeof(narrow));
		std::printf("%s\n", narrow);
		std::fflush(stdout);
		return IDOK;
	}
	int result = ShowMessageBoxX11(aText, aTitle, aType, aTimeout);
	if (!result)
	{
		// X11 failed for some other reason: fall back to the console.
		char narrow[msgbox_text_size];
		WideToNarrow(aText, narrow, sizeof(narrow));
		std::printf("%s\n", narrow);
		std::fflush(stdout);
		return IDOK;
	}
	return result;
}

bool LinuxInputBox(LPCTSTR aPrompt, LPCTSTR aTitle, LPCTSTR aDefault, LPTSTR aBuf, int aBufSize)
{
	if (!aBuf || aBufSize <= 0)
		return false;
	aBuf[0] = L'\0';

	if (!LinuxHasDisplay())
	{
		// Headless fallback: prompt on stdout, read a line from stdin.
		char narrow[4096];
		WideToNarrow(aPrompt, narrow, sizeof(narrow));
		std::printf("%s", narrow);
		std::fflush(stdout);
		char line[4096];
		if (!std::fgets(line, sizeof(line), stdin))
			return false;
		size_t n = strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = '\0';
		mbstowcs(aBuf, line, aBufSize - 1);
		aBuf[aBufSize - 1] = L'\0';
		return true;
	}

	Display *dpy = XOpenDisplay(nullptr);
	if (!dpy)
		return false;
	(void)aTitle;

	int scr = DefaultScreen(dpy);
	int win_w = 480, win_h = 130;
	Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr)
		, 100, 100, win_w, win_h, 1, BlackPixel(dpy, scr), WhitePixel(dpy, scr));
	Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(dpy, win, &wm_delete, 1);
	XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
	GC gc = XCreateGC(dpy, win, 0, nullptr);
	XFontStruct *font = XLoadQueryFont(dpy, "fixed");
	if (!font)
		font = XLoadQueryFont(dpy, "9x15");
	if (!font)
	{
		XDestroyWindow(dpy, win);
		XCloseDisplay(dpy);
		return false;
	}
	XSetFont(dpy, gc, font->fid);
	int font_height = font->ascent + font->descent;
	char title_narrow[512];
	WideToNarrow(aTitle && *aTitle ? aTitle : L"AutoHotkey Input", title_narrow, sizeof(title_narrow));
	XStoreName(dpy, win, title_narrow);

	// Entry text buffer (narrow for XLookupString simplicity; converted back).
	char entry[1024];
	entry[0] = '\0';
	size_t entry_len = 0;
	if (aDefault)
	{
		char defn[1024];
		WideToNarrow(aDefault, defn, sizeof(defn));
		size_t def_len = strlen(defn);
		if (def_len >= sizeof(entry))
			def_len = sizeof(entry) - 1;
		memcpy(entry, defn, def_len);
		entry[def_len] = '\0';
		entry_len = def_len;
	}
	char prompt_narrow[4096];
	WideToNarrow(aPrompt, prompt_narrow, sizeof(prompt_narrow));

	XMapRaised(dpy, win);
	XFlush(dpy);

	// Test hook: AHK_INPUTBOX_AUTOCLOSE_MS=<ms> auto-confirms the dialog with
	// the default text after the given delay, so automated suites can exercise
	// the X11 path without user interaction.
	int autoclose_ms = 0;
	if (const char *ac = getenv("AHK_INPUTBOX_AUTOCLOSE_MS"))
		autoclose_ms = atoi(ac);
	long long opened_at = LinuxNowMs();

	int result_id = IDCANCEL;
	XEvent ev;
	for (bool done = false; !done;)
	{
		if (autoclose_ms > 0 && LinuxNowMs() - opened_at >= autoclose_ms)
		{
			result_id = IDOK;
			done = true;
			break;
		}
		if (!XPending(dpy))
		{
			// Poll for events so the autoclose hook can fire.
			fd_set fds;
			FD_ZERO(&fds);
			int fd = ConnectionNumber(dpy);
			FD_SET(fd, &fds);
			struct timeval tv = {0, 50000}; // 50 ms
			if (select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0)
				continue;
		}
		XNextEvent(dpy, &ev);
		switch (ev.type)
		{
		case Expose:
			if (ev.xexpose.count == 0)
			{
				XClearWindow(dpy, win);
				// Prompt (wrapped, first 3 lines).
				char *line = prompt_narrow;
				for (int i = 0; i < 3 && line && *line; ++i)
				{
					char *eol = strchr(line, '\n');
					if (eol)
						*eol = '\0';
					XDrawString(dpy, win, gc, 10, 14 + font_height + i * font_height, line, (int)strlen(line));
					line = eol ? eol + 1 : nullptr;
				}
				// Entry box.
				int ey = 14 + 4 * font_height;
				XDrawRectangle(dpy, win, gc, 10, ey, win_w - 20, font_height + 10);
				XDrawString(dpy, win, gc, 16, ey + font_height + 2, entry, (int)entry_len);
				// OK / Cancel buttons.
				int by = win_h - 44;
				XDrawRectangle(dpy, win, gc, win_w - 190, by, 84, 28);
				XDrawString(dpy, win, gc, win_w - 190 + 28, by + 19, "OK", 2);
				XDrawRectangle(dpy, win, gc, win_w - 96, by, 84, 28);
				XDrawString(dpy, win, gc, win_w - 96 + 18, by + 19, "Cancel", 6);
			}
			break;
		case KeyPress:
			{
				char buf[32];
				KeySym ks;
				int n = XLookupString(&ev.xkey, buf, sizeof(buf), &ks, nullptr);
				if (ks == XK_Return || ks == XK_KP_Enter)
				{
					result_id = IDOK;
					done = true;
				}
				else if (ks == XK_Escape)
				{
					result_id = IDCANCEL;
					done = true;
				}
				else if (ks == XK_BackSpace)
				{
					if (entry_len > 0)
						entry[--entry_len] = '\0';
					// Redraw entry area.
					int ey = 14 + 4 * font_height;
					XClearArea(dpy, win, 11, ey + 1, win_w - 22, font_height + 8, True);
				}
				else if (n > 0 && entry_len + n < sizeof(entry))
				{
					memcpy(entry + entry_len, buf, n);
					entry_len += n;
					entry[entry_len] = '\0';
					int ey = 14 + 4 * font_height;
					XClearArea(dpy, win, 11, ey + 1, win_w - 22, font_height + 8, True);
				}
			}
			break;
		case ButtonPress:
			{
				int x = ev.xbutton.x, y = ev.xbutton.y;
				int by = win_h - 44;
				if (y >= by && y <= by + 28)
				{
					if (x >= win_w - 190 && x <= win_w - 106)
					{
						result_id = IDOK;
						done = true;
					}
					else if (x >= win_w - 96 && x <= win_w - 12)
					{
						result_id = IDCANCEL;
						done = true;
					}
				}
			}
			break;
		case ClientMessage:
			if ((Atom)ev.xclient.data.l[0] == wm_delete)
			{
				result_id = IDCANCEL;
				done = true;
			}
			break;
		default:
			break;
		}
	}

	if (result_id == IDOK)
		mbstowcs(aBuf, entry, aBufSize - 1);
	aBuf[aBufSize - 1] = L'\0';

	XFreeFont(dpy, font);
	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return result_id == IDOK;
}
