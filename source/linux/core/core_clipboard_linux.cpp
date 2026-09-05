// Linux system-clipboard layer.
//
// A_Clipboard / ClipWait on Linux integrate with the desktop clipboard:
//   - X11/XWayland: the X11 CLIPBOARD selection (UTF8_STRING).  Reading
//     converts the selection (synchronously, with a timeout, on the
//     script thread); writing takes ownership and serves SelectionRequest
//     events during main-loop/MsgSleep dispatches.
//   - Pure Wayland: wl_data_device via the seat (text/plain;charset=utf-8
//     and text/plain mime types).  Reading receives the current offer;
//     writing creates a data source that serves send requests during the
//     Wayland dispatch hook.
//   - No display backend: falls back to the process-internal storage
//     (previous behavior), so A_Clipboard still works headless.
//
// Windows semantics per docs-v2: A_Clipboard returns the clipboard text;
// assigning it replaces the clipboard.  ClipWait waits until the clipboard
// contains text (A_ClipboardWaitForData is the wait primitive).

#include "../../stdafx.h"
#include "../../script.h"
#include "../../globaldata.h"
#include "../../script_func_impl.h"
#include "core_clipboard_linux.h"
#include "core_win_linux.h"
#include "core_wayland_linux.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>
#include <wayland-client.h>
#include <dbus/dbus.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Process-internal fallback storage
// ---------------------------------------------------------------------------

static std::wstring &LinuxClipboardFallback()
{
	static std::wstring s;
	return s;
}

// Clipboard transaction timeout (ms).  Some clipboard owners (a slow app
// that only serves SelectionRequest after it finishes its own work) need
// longer than the default 2 s to answer a read; conversely a short value
// bounds a hung owner.  Configurable per environment so scripts can match
// their workload without a rebuild (check0820 regression: slow consumers):
//   AHK_CLIPBOARD_TIMEOUT_MS=<ms>   (default 2000)
static int LinuxClipTimeoutMs()
{
	static int s = -1;
	if (s == -1)
	{
		s = 2000;
		if (const char *v = getenv("AHK_CLIPBOARD_TIMEOUT_MS"))
		{
			long n = strtol(v, nullptr, 10);
			if (n > 0 && n <= 600000)
				s = (int)n;
			else
				fprintf(stderr, "AHK warning: AHK_CLIPBOARD_TIMEOUT_MS=%s "
					"ignored (need 1..600000)\n", v);
		}
	}
	return s;
}

// ---------------------------------------------------------------------------
// Text <-> UTF-8 (the clipboard interchange format)
// ---------------------------------------------------------------------------

static std::string LinuxWideToUtf8(const std::wstring &aWide)
{
	std::string out;
	for (size_t i = 0; i < aWide.size(); ++i)
	{
		unsigned int c = (unsigned int)aWide[i];
		// Be defensive if a caller supplied Windows-style UTF-16 units on
		// Linux: combine a valid pair before encoding, replace an orphan.
		if (c >= 0xD800 && c <= 0xDBFF)
		{
			if (i + 1 < aWide.size())
			{
				unsigned int low = (unsigned int)aWide[i + 1];
				if (low >= 0xDC00 && low <= 0xDFFF)
				{
					c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
					++i;
				}
				else
					c = 0xFFFD;
			}
			else
				c = 0xFFFD;
		}
		else if (c >= 0xDC00 && c <= 0xDFFF || c > 0x10FFFF)
			c = 0xFFFD;
		if (c < 0x80)
			out += (char)c;
		else if (c < 0x800)
		{
			out += (char)(0xC0 | (c >> 6));
			out += (char)(0x80 | (c & 0x3F));
		}
		else if (c < 0x10000)
		{
			out += (char)(0xE0 | (c >> 12));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		}
		else
		{
			out += (char)(0xF0 | (c >> 18));
			out += (char)(0x80 | ((c >> 12) & 0x3F));
			out += (char)(0x80 | ((c >> 6) & 0x3F));
			out += (char)(0x80 | (c & 0x3F));
		}
	}
	return out;
}

static bool LinuxUtf8ToWide(const std::string &aUtf8, std::wstring &aOut)
{
	aOut.clear();
	for (size_t i = 0; i < aUtf8.size();)
	{
		unsigned char c = (unsigned char)aUtf8[i];
		unsigned int cp = 0;
		size_t extra = 0;
		if (c <= 0x7F)
		{
			cp = c;
		}
		else if (c >= 0xC2 && c <= 0xDF)
		{
			cp = c & 0x1F;
			extra = 1;
		}
		else if (c >= 0xE0 && c <= 0xEF)
		{
			cp = c & 0x0F;
			extra = 2;
		}
		else if (c >= 0xF0 && c <= 0xF4)
		{
			cp = c & 0x07;
			extra = 3;
		}
		else
			return false;
		if (i + extra >= aUtf8.size())
			return false;
		for (size_t k = 1; k <= extra; ++k)
		{
			unsigned char cc = (unsigned char)aUtf8[i + k];
			if ((cc & 0xC0) != 0x80)
				return false;
			cp = (cp << 6) | (cc & 0x3F);
		}
		if ((extra == 1 && cp < 0x80)
			|| (extra == 2 && cp < 0x800)
			|| (extra == 3 && cp < 0x10000)
			|| (cp >= 0xD800 && cp <= 0xDFFF)
			|| cp > 0x10FFFF)
			return false;
		aOut.push_back((wchar_t)cp);
		i += extra + 1;
	}
	return true;
}

// ---------------------------------------------------------------------------
// X11 CLIPBOARD selection
// ---------------------------------------------------------------------------

// Paste transaction flags (check0820 P1): shared by the Wayland source-send
// handler and the X11 SelectionRequest handler, which both run before the
// public paste API below.  File scope so either handler can observe them.
static bool sPasteActive = false;
static bool sPasteServed = false;
static bool sPasteOwnershipLost = false;

namespace {

Display *gClipX11Display = nullptr;
Window gClipX11Window = 0;        // Hidden window: ownership + event target.
Atom gClipX11Utf8 = 0;
Atom gClipX11Prop = 0;            // Property name used for transfers.
Atom gClipX11Clipboard = 0;       // CLIPBOARD selection atom.
Atom gClipX11Targets = 0;         // TARGETS atom.
Atom gClipX11Incr = 0;            // INCR large-transfer marker.
std::wstring gClipX11Data;        // Our owned data.
bool gClipX11Owned = false;

// Clipboard-change watch (check_detail0821 §4): when an OnClipboardChange
// handler is registered, XFixes selection tracking on the CLIPBOARD
// selection reports ownership changes; LinuxClipboardDispatchX11 then
// delivers the callback with the Windows Type argument.
bool gClipX11Watch = false;
int gClipXfixesEventBase = -1;   // XFixes event base (cached at start).
Atom gClipX11TextAtom = 0;       // UTF8_STRING (text => Type 1).

// True while a synchronous read is waiting for SelectionNotify.
bool gClipX11Reading = false;
bool gClipX11ReadDone = false;
bool gClipX11ReadFailed = false;

int LinuxClipX11ErrorHandler(Display *d, XErrorEvent *e)
{
	// BadWindow etc. during clipboard transfers are expected (owner died
	// mid-transfer); mark the read failed and continue.
	if (gClipX11Reading)
		gClipX11ReadFailed = true;
	return 0;
}

void LinuxClipX11Ensure(Display *d)
{
	if (gClipX11Display == d && gClipX11Window)
		return;
	gClipX11Display = d;
	gClipX11Window = XCreateSimpleWindow(d, DefaultRootWindow(d), -10, -10, 1, 1, 0, 0, 0);
	// Selection owners receive SelectionRequest/SelectionClear without an
	// event mask; no XSelectInput needed.
	gClipX11Utf8 = XInternAtom(d, "UTF8_STRING", False);
	gClipX11Prop = XInternAtom(d, "AHK_CLIPBOARD", False);
	gClipX11Clipboard = XInternAtom(d, "CLIPBOARD", False);
	gClipX11Targets = XInternAtom(d, "TARGETS", False);
	gClipX11Incr = XInternAtom(d, "INCR", False);
	gClipX11TextAtom = gClipX11Utf8;
}

} // namespace
// Forward declarations (used by the read path and the P2-6 block below).
static void LinuxClipX11ServeRequest(Display *d, XSelectionRequestEvent *req);
// ---------------------------------------------------------------------------
// P2-6 (check_detail0901 §18): multi-representation snapshot.
// ---------------------------------------------------------------------------
// The Windows ClipboardAll binary walk (var.cpp) persists a {format,size,
// data} chain.  On Linux the same chain is filled from the MIME snapshot
// below: format ids are the CF_* constants for the text entry (13 =
// CF_UNICODETEXT) and 0xC100+i for the extra representations (registered
// formats on Windows are >= 0xC000, so restore accepts those too).  The
// snapshot itself is an AHKCB1 container: magic, version, item count, then
// per item {mime_len u16, mime bytes, data_len u32, fnv1a checksum u32,
// data bytes} — version/length/checksum/limits per the audit.

#define AHK_CB_SNAPSHOT_MAGIC   0x31424B48u /* "HKB1" */
#define AHK_CB_SNAPSHOT_VERSION 1u
#define AHK_CB_MAX_ITEM_BYTES   (32u * 1024u * 1024u)   // per-item bound
#define AHK_CB_MAX_TOTAL_BYTES  (128u * 1024u * 1024u)  // total bound

struct LinuxClipboardItem
{
	std::string mime;
	std::vector<unsigned char> data;
};
struct LinuxClipboardSnapshot
{
	std::vector<LinuxClipboardItem> items;
	uint64_t owner_generation = 0;
};
static LinuxClipboardSnapshot gClipSnapshot;   // Data WE own and serve.
static LinuxClipboardSnapshot gClipSavedAll;   // Last ClipboardAll() read.
static uint64_t gClipOwnerGen = 1;             // Bumps on every write.

static uint32_t LinuxClipFnv1a(const unsigned char *a, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; ++i)
	{
		h ^= a[i];
		h *= 16777619u;
	}
	return h;
}

static bool LinuxClipMimeSafe(const std::string &aMime)
{
	if (aMime.empty() || aMime.size() > 255)
		return false;
	for (unsigned char ch : aMime)
		if (ch < 0x21 || ch > 0x7E) // no controls, whitespace or NUL
			return false;
	return true;
}

static bool LinuxClipSnapshotSerialize(const LinuxClipboardSnapshot &aSnap,
	std::vector<unsigned char> &aOut)
{
	if (aSnap.items.size() > 256)
		return false;
	size_t total = 12;
	for (auto &item : aSnap.items)
	{
		if (!LinuxClipMimeSafe(item.mime)
			|| item.data.size() > AHK_CB_MAX_ITEM_BYTES)
			return false;
		size_t added = 2 + item.mime.size() + 4 + 4 + item.data.size();
		if (added > AHK_CB_MAX_TOTAL_BYTES
			|| total > AHK_CB_MAX_TOTAL_BYTES - added)
			return false;
		total += added;
	}
	aOut.clear();
	aOut.reserve(total);
	auto put32 = [&aOut](uint32_t v) {
		aOut.push_back((unsigned char)(v & 0xFF));
		aOut.push_back((unsigned char)((v >> 8) & 0xFF));
		aOut.push_back((unsigned char)((v >> 16) & 0xFF));
		aOut.push_back((unsigned char)((v >> 24) & 0xFF));
	};
	auto put16 = [&aOut](uint16_t v) {
		aOut.push_back((unsigned char)(v & 0xFF));
		aOut.push_back((unsigned char)((v >> 8) & 0xFF));
	};
	put32(AHK_CB_SNAPSHOT_MAGIC);
	put32(AHK_CB_SNAPSHOT_VERSION);
	put32((uint32_t)aSnap.items.size());
	for (auto &item : aSnap.items)
	{
		if (item.mime.size() > 0xFFFF || item.data.size() > AHK_CB_MAX_ITEM_BYTES)
			return false;
		put16((uint16_t)item.mime.size());
		aOut.insert(aOut.end(), item.mime.begin(), item.mime.end());
		put32((uint32_t)item.data.size());
		put32(LinuxClipFnv1a(item.data.data(), item.data.size()));
		aOut.insert(aOut.end(), item.data.begin(), item.data.end());
	}
	return true;
}

static bool LinuxClipSnapshotDeserialize(const unsigned char *a, size_t n,
	LinuxClipboardSnapshot &aSnap)
{
	aSnap.items.clear();
	if (n < 12)
		return false;
	auto get32 = [&a](size_t &i) -> uint32_t {
		uint32_t v = (uint32_t)a[i] | ((uint32_t)a[i + 1] << 8)
			| ((uint32_t)a[i + 2] << 16) | ((uint32_t)a[i + 3] << 24);
		i += 4;
		return v;
	};
	auto get16 = [&a](size_t &i) -> uint16_t {
		uint16_t v = (uint16_t)(a[i] | (a[i + 1] << 8));
		i += 2;
		return v;
	};
	size_t i = 0;
	uint32_t magic = get32(i), version = get32(i), count = get32(i);
	if (magic != AHK_CB_SNAPSHOT_MAGIC || version != AHK_CB_SNAPSHOT_VERSION
		|| count > 256)
		return false;
	size_t total = 0;
	for (uint32_t k = 0; k < count; ++k)
	{
		if (i + 2 > n)
			return false;
		uint16_t mime_len = get16(i);
		if (i + (size_t)mime_len + 8 > n)
			return false;
		LinuxClipboardItem item;
		item.mime.assign((const char *)a + i, mime_len);
		i += mime_len;
		if (!LinuxClipMimeSafe(item.mime))
			return false;
		for (const auto &existing : aSnap.items)
			if (existing.mime == item.mime)
				return false;
		uint32_t data_len = get32(i), checksum = get32(i);
		if (i + (size_t)data_len > n || data_len > AHK_CB_MAX_ITEM_BYTES)
			return false;
		total += (size_t)data_len;
		if (total > AHK_CB_MAX_TOTAL_BYTES)
			return false;
		item.data.assign(a + i, a + i + data_len);
		i += data_len;
		if (checksum != LinuxClipFnv1a(item.data.data(), item.data.size()))
			return false; // Corrupted item: reject the whole snapshot.
		aSnap.items.push_back(std::move(item));
	}
	return true;
}

// Find one representation by exact MIME match.
static const LinuxClipboardItem *LinuxClipSnapshotFind(
	const LinuxClipboardSnapshot &aSnap, const char *aMime)
{
	for (auto &item : aSnap.items)
		if (item.mime == aMime)
			return &item;
	return nullptr;
}

// The text entry: a synthesized text/plain representation so the text
// round-trip needs no special case in the reader.
static void LinuxClipSnapshotEnsureText(LinuxClipboardSnapshot &aSnap,
	const std::wstring &aText)
{
	std::string utf8 = LinuxWideToUtf8(aText);
	for (auto &item : aSnap.items)
		if (item.mime == "text/plain;charset=utf-8"
			|| item.mime == "text/plain")
		{
			item.data.assign(utf8.begin(), utf8.end());
			return;
		}
	// Insert first (the preferred representation comes first, audit §18).
	LinuxClipboardItem item;
	item.mime = "text/plain;charset=utf-8";
	item.data.assign(utf8.begin(), utf8.end());
	aSnap.items.insert(aSnap.items.begin(), std::move(item));
}

static bool LinuxClipX11RequestTarget(Display *d, Atom aTarget,
	std::vector<unsigned char> &aOut)
{
	LinuxClipX11Ensure(d);
	gClipX11Reading = true;
	gClipX11ReadDone = false;
	gClipX11ReadFailed = false;
	aOut.clear();
	XConvertSelection(d, gClipX11Clipboard, aTarget, gClipX11Prop,
		gClipX11Window, CurrentTime);
	XFlush(d);
	const unsigned long max_longs =
		(unsigned long)(AHK_CB_MAX_ITEM_BYTES / 4u + 1u);
	const DWORD started = GetTickCount();
	bool incr = false;
	bool transfer_done = false;
	auto read_property_chunk = [&](bool &aDone) -> bool {
		Atom type = None;
		int format = 0;
		unsigned long nitems = 0, after = 0;
		unsigned char *data = nullptr;
		if (XGetWindowProperty(d, gClipX11Window, gClipX11Prop,
				0, max_longs, True, AnyPropertyType, &type, &format,
				&nitems, &after, &data) != Success)
			return false;
		if (type == gClipX11Incr)
		{
			XFree(data);
			return false; // INCR is only valid for the initial property.
		}
		size_t bytes = format == 32
			? (size_t)nitems * sizeof(unsigned long)
			: format == 16 ? (size_t)nitems * 2 : (size_t)nitems;
		if (after != 0 || bytes > AHK_CB_MAX_ITEM_BYTES
			|| bytes > AHK_CB_MAX_ITEM_BYTES - aOut.size())
		{
			XFree(data);
			return false;
		}
		if (bytes && data)
			aOut.insert(aOut.end(), data, data + bytes);
		XFree(data);
		aDone = bytes == 0;
		return true;
	};
	while (!gClipX11ReadDone && !gClipX11ReadFailed
		&& (DWORD)(GetTickCount() - started) < (DWORD)LinuxClipTimeoutMs())
	{
		struct pollfd pfd = {ConnectionNumber(d), POLLIN, 0};
		if (poll(&pfd, 1, 20) <= 0)
			continue;
		while (XPending(d) > 0)
		{
			XEvent ev;
			XNextEvent(d, &ev);
			if (ev.type == SelectionNotify
				&& ev.xselection.requestor == gClipX11Window)
			{
				if (ev.xselection.property == None)
				{
					gClipX11ReadFailed = true;
					break;
				}
				Atom type = None;
				int format = 0;
				unsigned long nitems = 0, after = 0;
				unsigned char *data = nullptr;
				if (XGetWindowProperty(d, gClipX11Window, gClipX11Prop,
						0, max_longs, True, AnyPropertyType, &type, &format,
						&nitems, &after, &data) != Success)
				{
					gClipX11ReadFailed = true;
					break;
				}
				if (type == gClipX11Incr)
				{
					// The initial INCR value is only a size hint. Subsequent
					// chunks arrive as PropertyNotify(NewValue) events.
					incr = true;
					transfer_done = false;
					XSelectInput(d, gClipX11Window, PropertyChangeMask);
					XFree(data);
					continue;
				}
				size_t bytes = format == 32
					? (size_t)nitems * sizeof(unsigned long)
					: format == 16 ? (size_t)nitems * 2 : (size_t)nitems;
				if (after != 0 || bytes > AHK_CB_MAX_ITEM_BYTES)
					gClipX11ReadFailed = true;
				else if (bytes && data)
					aOut.assign(data, data + bytes);
				XFree(data);
				if (!gClipX11ReadFailed)
				{
					gClipX11ReadDone = true;
					break;
				}
				break;
			}
			if (incr && ev.type == PropertyNotify
				&& ev.xproperty.window == gClipX11Window
				&& ev.xproperty.atom == gClipX11Prop
				&& ev.xproperty.state == PropertyNewValue)
			{
				if (!read_property_chunk(transfer_done))
				{
					gClipX11ReadFailed = true;
					break;
				}
				if (transfer_done)
				{
					gClipX11ReadDone = true;
					break;
				}
			}
			if (ev.type == SelectionRequest)
				LinuxClipX11ServeRequest(d, &ev.xselectionrequest);
		}
	}
	// Do not leave PropertyNotify selected: dispatch would otherwise keep
	// re-seeing stale property events and starve SelectionRequest handling.
	if (incr)
		XSelectInput(d, gClipX11Window, 0);
	gClipX11Reading = false;
	return gClipX11ReadDone && !gClipX11ReadFailed;
}

// Serve a TARGETS request from OUR snapshot (or the text-only legacy list).
static void LinuxClipX11ServeTargets(Display *d, XSelectionRequestEvent *req)
{
	std::vector<Atom> atoms;
	atoms.push_back(gClipX11Targets);
	atoms.push_back(XA_STRING);
	atoms.push_back(gClipX11Utf8);
	std::vector<Atom> extra;
	for (auto &item : gClipSnapshot.items)
	{
		if (item.mime == "text/plain;charset=utf-8"
			|| item.mime == "text/plain")
			continue;
		Atom a = XInternAtom(d, item.mime.c_str(), True);
		if (a)
			extra.push_back(a);
	}
	atoms.insert(atoms.end(), extra.begin(), extra.end());
	XChangeProperty(d, req->requestor, req->property, XA_ATOM, 32,
		PropModeReplace, (unsigned char *)atoms.data(), (int)atoms.size());
}

// Forward declaration (defined below; used by the read path).
static void LinuxClipX11ServeRequest(Display *d, XSelectionRequestEvent *req);

// Read: request the CLIPBOARD selection and wait (bounded) for the result.
static bool LinuxClipboardX11Read(Display *d, std::wstring &aText)
{
	aText.clear();
	LinuxClipX11Ensure(d);
	if (!XGetSelectionOwner(d, gClipX11Clipboard))
		return true; // Empty clipboard, no error.
	std::vector<unsigned char> raw;
	if (!LinuxClipX11RequestTarget(d, gClipX11Utf8, raw)
		&& !LinuxClipX11RequestTarget(d, XA_STRING, raw))
		return false;
	return LinuxUtf8ToWide(std::string(raw.begin(), raw.end()), aText);
}

// Write: take ownership; serve SelectionRequest events later.
static bool LinuxClipboardX11Write(Display *d, const std::wstring &aText)
{
	LinuxClipX11Ensure(d);
	gClipX11Data = aText;
	// P2-6: a plain text write collapses the snapshot to its text entry.
	gClipSnapshot.items.clear();
	gClipSnapshot.owner_generation = ++gClipOwnerGen;
	LinuxClipSnapshotEnsureText(gClipSnapshot, aText);
	XSetSelectionOwner(d, gClipX11Clipboard, gClipX11Window, CurrentTime);
	XFlush(d);
	gClipX11Owned = (XGetSelectionOwner(d, gClipX11Clipboard) == gClipX11Window);
	return true;
}

// Serve one SelectionRequest event (shared by the dispatch loop and the
// synchronous read path, where the event has already been dequeued).
static void LinuxClipX11ServeRequest(Display *d, XSelectionRequestEvent *req)
{
	std::string utf8 = LinuxWideToUtf8(gClipX11Data);
	XSelectionEvent sel;
	sel.type = SelectionNotify;
	sel.display = req->display;
	sel.requestor = req->requestor;
	sel.selection = req->selection;
	sel.target = req->target;
	sel.time = req->time;
	sel.property = req->property;
	if (req->target == gClipX11Targets)
	{
		LinuxClipX11ServeTargets(d, req);
	}
	else if (req->target == gClipX11Utf8 || req->target == XA_STRING)
	{
		XChangeProperty(d, req->requestor, req->property, req->target, 8,
			PropModeReplace, (unsigned char *)utf8.data(), (int)utf8.size());
	}
	else
	{
		// P2-6: serve any MIME representation we hold (a TARGETS walk asks
		// for non-text atoms by name).
		char *name = XGetAtomName(d, req->target);
		const LinuxClipboardItem *item = name
			? LinuxClipSnapshotFind(gClipSnapshot, name) : nullptr;
		if (item)
		{
			XChangeProperty(d, req->requestor, req->property, req->target, 8,
				PropModeReplace, item->data.data(), (int)item->data.size());
		}
		else
			sel.property = None;
		if (name)
			XFree(name);
	}
	XSendEvent(d, req->requestor, False, 0, (XEvent *)&sel);
	XFlush(d);
	// A clipboard-paste transaction asked for our data: the target pulled
	// the offer, so the paste is consumed (see PasteWaitConsumed).
	if (sPasteActive)
		sPasteServed = true;
}

void LinuxClipboardDispatchX11(Display *d)
{
	if (!d || d != gClipX11Display)
		return;
	// Without ownership AND without an active watch there is nothing to do
	// (the read path handles its own events synchronously in its own loop).
	if (!gClipX11Owned && !gClipX11Watch)
		return;
	// A clipboard owner that does not process events loses the selection;
	// serve requests and clear ownership on SelectionClear.  Events that
	// are not clipboard-related are put back so callers waiting for a
	// SelectionNotify still see it.  A clipboard-change watch additionally
	// consumes XFixes SelectionNotify (SetSelectionOwner / ClientClose /
	// WindowDestroy) events and fires the OnClipboardChange callback.
	while (XPending(d) > 0)
	{
		XEvent ev;
		XNextEvent(d, &ev);
		if (ev.type == SelectionClear)
		{
			gClipX11Owned = false;
			if (sPasteActive)
				sPasteOwnershipLost = true;
			continue;
		}
		if (ev.type == SelectionRequest)
		{
			LinuxClipX11ServeRequest(d, &ev.xselectionrequest);
			continue;
		}
		if (ev.type == SelectionNotify && ev.xselection.requestor == gClipX11Window)
		{
			// A clipboard READ on this connection handles its SelectionNotify
			// in its own poll loop, so this should not normally be seen here;
			// put it back to be safe.
			XPutBackEvent(d, &ev);
			break;
		}
		if (gClipX11Watch && gClipXfixesEventBase >= 0
			&& ev.type == gClipXfixesEventBase + XFixesSelectionNotify)
		{
			// XFixes selection-tracking events are delivered on the client
			// connection that selected them (libXfixes extends XEvent; the
			// canonical access is a cast, as in clipnotify).
			XFixesSelectionNotifyEvent *xev = (XFixesSelectionNotifyEvent *)&ev;
			if (xev->selection != gClipX11Clipboard)
				continue;
			// XFixes reports every owner change (including ours).  Fire only
			// when the watch is armed and the script still has a handler.
			if (!g_script.mOnClipboardChange.Count())
				continue;
			// Determine Type non-blockingly (no TARGETS round-trip here: that
			// probe blocked the main loop and could live-lock when the owner
			// is a window of this very connection).  Type = 0 only when the
			// selection has no owner (ClientClose/WindowDestroy followed by
			// no new owner); any living owner is reported as non-empty.
			// Text-vs-nontext (Windows Type 1 vs 2) is approximated as "has
			// an owner" -> 1; exact TARGETS probing is deferred to the
			// read path (A_Clipboard) which already distinguishes.
			int type = 1;
			Window owner = XGetSelectionOwner(d, gClipX11Clipboard);
			if (!owner)
				type = 0;
			// Fire the callback (Windows delivers it via the message pump,
			// which is exactly where we are now: the main dispatch hook).
			if (!g_script.mOnClipboardChangeIsRunning)
			{
				ExprTokenType param((__int64)type);
				g_script.mOnClipboardChangeIsRunning = true;
				g_script.mOnClipboardChange.Call(&param, 1, 1);
				g_script.mOnClipboardChangeIsRunning = false;
			}
			continue;
		}
		// Not ours: put it back so waiters (hotkey/clipboard read) see it.
		XPutBackEvent(d, &ev);
		break;
	}
}

// ---------------------------------------------------------------------------
// Clipboard-change watch (OnClipboardChange)
// ---------------------------------------------------------------------------

// GNOME-extension listener (check_detail0821 §4 / R2): Mutter does not
// implement the Wayland ext-data-control protocol, so the GNOME Shell
// extension broadcasts ClipboardChanged(u type) on the session bus (bus name
// io.github.autohotkey.GlobalHotkeys1).  When a script registers
// OnClipboardChange on a session without X11/XFixes (pure Wayland), this
// listener arms a session-bus filter and LinuxClipboardDispatchWayland pumps
// it from the main loop.  The signal carries only the Type (1 = new owner,
// 0 = cleared); the runtime reads the clipboard itself when the callback
// asks for A_Clipboard.
static DBusConnection *gClipExtConn = nullptr;

static DBusHandlerResult LinuxClipExtFilter(DBusConnection *aConn, DBusMessage *aMsg, void *aData)
{
	if (dbus_message_is_signal(aMsg, "io.github.autohotkey.GlobalHotkeys1", "ClipboardChanged"))
	{
		dbus_uint32_t type = 1;
		if (!dbus_message_get_args(aMsg, nullptr, DBUS_TYPE_UINT32, &type, DBUS_TYPE_INVALID))
			type = 1;
		// Fire the callback (same contract as the XFixes path: delivered on
		// the main dispatch hook, guarded against re-entry).
		if (g_script.mOnClipboardChange.Count())
		{
			if (!g_script.mOnClipboardChangeIsRunning)
			{
				ExprTokenType param((__int64)(int)type);
				g_script.mOnClipboardChangeIsRunning = true;
				g_script.mOnClipboardChange.Call(&param, 1, 1);
				g_script.mOnClipboardChangeIsRunning = false;
			}
		}
		return DBUS_HANDLER_RESULT_HANDLED;
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// Arm the extension listener; returns true only when the extension's bus
// name is present (so a bare session fails closed without the extension).
static bool LinuxClipExtStart()
{
	if (gClipExtConn)
		return true;
	DBusError err;
	dbus_error_init(&err);
	DBusConnection *bus = dbus_bus_get(DBUS_BUS_SESSION, &err);
	if (!bus)
	{
		dbus_error_free(&err);
		return false;
	}
	dbus_bool_t owned = dbus_bus_name_has_owner(bus, "io.github.autohotkey.GlobalHotkeys1", &err);
	dbus_error_free(&err);
	if (!owned)
	{
		dbus_connection_unref(bus);
		return false;
	}
	gClipExtConn = bus; // Reuse the shared session connection (refcounted).
	dbus_connection_set_exit_on_disconnect(gClipExtConn, FALSE);
	dbus_connection_add_filter(gClipExtConn, LinuxClipExtFilter, nullptr, nullptr);
	// The shared session connection accumulates match rules from other
	// components (the portal backend etc.); once any rule exists, broadcast
	// signals not matching one are filtered out.  Add ours explicitly.
	dbus_bus_add_match(gClipExtConn
		, "type='signal',interface='io.github.autohotkey.GlobalHotkeys1',member='ClipboardChanged'"
		, &err);
	if (dbus_error_is_set(&err))
		dbus_error_free(&err);
	return true;
}

bool LinuxClipboardWatchActive()
{
	return gClipX11Watch || (gClipExtConn != nullptr);
}

bool LinuxClipboardWatchStart()
{
	Display *d = LinuxX11Display();
	if (d)
	{
		LinuxClipX11Ensure(d);
		if (gClipX11Watch)
			return true;
		int xfixes_event_base = 0, xfixes_error_base = 0;
		if (!XFixesQueryExtension(d, &xfixes_event_base, &xfixes_error_base))
			return false;
		Window root = DefaultRootWindow(d);
		XFixesSelectSelectionInput(d, root, gClipX11Clipboard
			, XFixesSetSelectionOwnerNotifyMask
			| XFixesSelectionClientCloseNotifyMask
			| XFixesSelectionWindowDestroyNotifyMask);
		XFlush(d);
		gClipXfixesEventBase = xfixes_event_base;
		gClipX11Watch = true;
		return true;
	}
	// Pure Wayland (no X11): the GNOME-extension signal path.
	return LinuxClipExtStart();
}

bool LinuxClipboardWatchStop()
{
	gClipX11Watch = false;
	if (gClipExtConn)
	{
		dbus_connection_remove_filter(gClipExtConn, LinuxClipExtFilter, nullptr);
		dbus_connection_unref(gClipExtConn);
		gClipExtConn = nullptr;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Wayland data device
// ---------------------------------------------------------------------------

namespace {

wl_data_device_manager *gClipWlMgr = nullptr;
wl_data_device *gClipWlDevice = nullptr;
wl_data_source *gClipWlSource = nullptr;
std::wstring gClipWlData;          // Data we offer when we own the clipboard.
wl_data_offer *gClipWlOffer = nullptr; // Current selection offer.
bool gClipWlOfferHasText = false;  // Offer advertises a text mime.
int gClipWlOfferFd = -1;           // Pipe for the received data.
std::string gClipWlOfferData;      // Received offer contents.
bool gClipWlOfferDone = false;

void LinuxClipWlOfferMime(void *aData, wl_data_offer *aOffer, const char *aMime)
{
	if (aMime && (!strcmp(aMime, "text/plain;charset=utf-8") || !strcmp(aMime, "text/plain")))
		gClipWlOfferHasText = true;
}

void LinuxClipWlOfferSourceActions(void *aData, wl_data_offer *aOffer, uint32_t aActions)
{
}

void LinuxClipWlOfferAction(void *aData, wl_data_offer *aOffer, uint32_t aAction)
{
}

static const wl_data_offer_listener sClipWlOfferListener = {
	LinuxClipWlOfferMime,
	LinuxClipWlOfferSourceActions,
	LinuxClipWlOfferAction,
};

void LinuxClipWlDeviceOffer(void *aData, wl_data_device *aDevice, wl_data_offer *aOffer)
{
	gClipWlOffer = aOffer;
	gClipWlOfferHasText = false;
	wl_data_offer_add_listener(aOffer, &sClipWlOfferListener, nullptr);
}

void LinuxClipWlDeviceEnter(void *aData, wl_data_device *aDevice, uint32_t aSerial
	, wl_surface *aSurface, wl_fixed_t aX, wl_fixed_t aY, wl_data_offer *aOffer)
{
}

void LinuxClipWlDeviceLeave(void *aData, wl_data_device *aDevice)
{
}

void LinuxClipWlDeviceMotion(void *aData, wl_data_device *aDevice, uint32_t aTime, wl_fixed_t aX, wl_fixed_t aY)
{
}

void LinuxClipWlDeviceDrop(void *aData, wl_data_device *aDevice)
{
}

void LinuxClipWlDeviceSelection(void *aData, wl_data_device *aDevice, wl_data_offer *aOffer)
{
	gClipWlOffer = aOffer;
	gClipWlOfferHasText = false;
	if (aOffer)
		wl_data_offer_add_listener(aOffer, &sClipWlOfferListener, nullptr);
}

static const wl_data_device_listener sClipWlDeviceListener = {
	LinuxClipWlDeviceOffer,    // data_offer
	LinuxClipWlDeviceEnter,    // enter
	LinuxClipWlDeviceLeave,    // leave
	LinuxClipWlDeviceMotion,   // motion
	LinuxClipWlDeviceDrop,     // drop
	LinuxClipWlDeviceSelection // selection
};

void LinuxClipWlSourceSend(void *aData, wl_data_source *aSource, const char *aMime, int32_t aFd)
{
	std::string utf8 = LinuxWideToUtf8(gClipWlData);
	bool complete = true;
	if (aFd >= 0)
	{
		size_t off = 0;
		while (off < utf8.size())
		{
			ssize_t n = write(aFd, utf8.data() + off, utf8.size() - off);
			if (n > 0)
			{
				off += (size_t)n;
				continue;
			}
			if (n < 0 && errno == EINTR)
				continue;
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
			{
				struct pollfd pfd = {aFd, POLLOUT, 0};
				if (poll(&pfd, 1, 50) > 0 && (pfd.revents & POLLOUT))
					continue;
			}
			complete = false;
			break;
		}
		close(aFd);
		if (sPasteActive && complete)
			sPasteServed = true; // The app pulled and received the full offer.
	}
}

void LinuxClipWlSourceCancelled(void *aData, wl_data_source *aSource)
{
	if (aSource != gClipWlSource)
		return;
	if (sPasteActive)
		sPasteOwnershipLost = true;
	// The protocol declares the source cancelled; do not retain a stale
	// non-null proxy that would make PasteRestore believe we still own it.
	gClipWlSource = nullptr;
	wl_data_source_destroy(aSource);
}

void LinuxClipWlSourceTarget(void *aData, wl_data_source *aSource, const char *aMime)
{
}

void LinuxClipWlSourceDndDropPerformed(void *aData, wl_data_source *aSource)
{
}

void LinuxClipWlSourceDndFinished(void *aData, wl_data_source *aSource)
{
}

void LinuxClipWlSourceAction(void *aData, wl_data_source *aSource, uint32_t aAction)
{
}

static const wl_data_source_listener sClipWlSourceListener = {
	LinuxClipWlSourceTarget,      // target
	LinuxClipWlSourceSend,        // send
	LinuxClipWlSourceCancelled,   // cancelled
	LinuxClipWlSourceDndDropPerformed,
	LinuxClipWlSourceDndFinished,
	LinuxClipWlSourceAction,
};

// Expose the Wayland ownership state to the paste transaction below (file
// scope outside the anonymous namespace; the paste code may not include
// Wayland headers directly).
bool LinuxClipWlOwnsSelection()
{
	return gClipWlSource != nullptr && !sPasteOwnershipLost;
}

} // namespace

// The Wayland registry hook (called from core_wayland_linux.cpp):
void LinuxClipboardWaylandRegistry(void *aData, wl_registry *aReg, uint32_t aName, const char *aIface, uint32_t aVersion)
{
	if (!strcmp(aIface, "wl_data_device_manager") && !gClipWlMgr)
		gClipWlMgr = (wl_data_device_manager *)wl_registry_bind(aReg, aName,
			&wl_data_device_manager_interface, 3);
}

void LinuxClipboardWaylandSeat(wl_seat *aSeat)
{
	if (gClipWlMgr && !gClipWlDevice && aSeat)
	{
		gClipWlDevice = wl_data_device_manager_get_data_device(gClipWlMgr, aSeat);
		wl_data_device_add_listener(gClipWlDevice, &sClipWlDeviceListener, nullptr);
	}
}

void LinuxClipboardWaylandTeardown()
{
	if (sPasteActive)
		sPasteOwnershipLost = true;
	if (gClipWlSource)
	{
		wl_data_source_destroy(gClipWlSource);
		gClipWlSource = nullptr;
	}
	if (gClipWlDevice)
	{
		// wl_data_device has no generated destroy wrapper in the system
		// header set; wl_proxy_destroy is the generic teardown for it.
		wl_proxy_destroy((struct wl_proxy *)gClipWlDevice);
		gClipWlDevice = nullptr;
	}
	if (gClipWlMgr)
	{
		wl_data_device_manager_destroy(gClipWlMgr);
		gClipWlMgr = nullptr;
	}
}

// Pump the GNOME-extension clipboard listener (D-Bus, display-independent).
// Called from LinuxInputBackendDispatch on every main-loop pass.
void LinuxClipboardDispatchWayland()
{
	if (!gClipExtConn)
		return;
	if (dbus_connection_read_write_dispatch(gClipExtConn, 0) == FALSE)
	{
		// Session bus dropped (session end / bus restart): tear down so the
		// next watch start re-arms cleanly.
		dbus_connection_remove_filter(gClipExtConn, LinuxClipExtFilter, nullptr);
		dbus_connection_unref(gClipExtConn);
		gClipExtConn = nullptr;
	}
}

// Dispatch Wayland events until aCond or aTimeoutMs; returns true when
// aCond.  Uses the same connection as the main Wayland layer.
static bool LinuxClipWlWait(wl_display *dpy, bool &aCond, int aTimeoutMs)
{
	int waited = 0;
	while (!aCond && waited < aTimeoutMs)
	{
		wl_display_flush(dpy);
		struct pollfd pfd;
		pfd.fd = wl_display_get_fd(dpy);
		pfd.events = POLLIN;
		pfd.revents = 0;
		int pr = poll(&pfd, 1, 50);
		if (pr > 0)
		{
			if (wl_display_dispatch(dpy) < 0)
				return false;
		}
		else if (pr < 0 && errno != EINTR)
			return false;
		waited += 50;
	}
	return aCond;
}

// Read: the data_device listener receives the selection offer; ask for
// the text mime and read the pipe synchronously.
static bool LinuxClipboardWaylandRead(wl_display *dpy, std::wstring &aText)
{
	aText.clear();
	if (!gClipWlDevice)
		return false;
	// Give the compositor a moment to deliver the current selection
	// (bounded by the configured transaction timeout; usually <100 ms).
	gClipWlOfferHasText = false;
	if (!LinuxClipWlWait(dpy, gClipWlOfferHasText, LinuxClipTimeoutMs()))
		return false;
	if (!gClipWlOffer)
		return false;
	// Receive into a pipe.
	int fds[2];
	if (pipe(fds) != 0)
		return false;
	gClipWlOfferDone = false;
	gClipWlOfferData.clear();
	wl_data_offer_receive(gClipWlOffer, "text/plain;charset=utf-8", fds[1]);
	close(fds[1]);
	wl_display_flush(dpy);
	// Read whatever the compositor writes (bounded by the configured
	// transaction timeout; the offer wait above already consumed part of
	// the same budget).
	std::vector<char> buf(65536);
	time_t deadline = time(nullptr) + LinuxClipTimeoutMs() / 1000;
	if (LinuxClipTimeoutMs() % 1000)
		++deadline; // Round up partial seconds.
	while (time(nullptr) < deadline)
	{
		struct pollfd pfd;
		pfd.fd = fds[0];
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, 50) > 0)
		{
			ssize_t n = read(fds[0], buf.data(), buf.size());
			if (n > 0)
			{
				if ((size_t)n > AHK_CB_MAX_ITEM_BYTES - gClipWlOfferData.size())
				{
					close(fds[0]);
					return false;
				}
				gClipWlOfferData.append(buf.data(), (size_t)n);
			}
			else
				break;
		}
	}
	close(fds[0]);
	// Offer must be finished (destroyed by the compositor after receive).
	if (gClipWlOffer)
	{
		wl_data_offer_destroy(gClipWlOffer);
		gClipWlOffer = nullptr;
	}
	if (!LinuxUtf8ToWide(gClipWlOfferData, aText))
	{
		aText.clear();
		return false;
	}
	return true;
}

static bool LinuxClipboardWaylandWrite(wl_display *dpy, const std::wstring &aText)
{
	if (!gClipWlMgr || !gClipWlDevice)
		return false;
	gClipWlData = aText;
	if (gClipWlSource)
		wl_data_source_destroy(gClipWlSource);
	gClipWlSource = wl_data_device_manager_create_data_source(gClipWlMgr);
	wl_data_source_add_listener(gClipWlSource, &sClipWlSourceListener, nullptr);
	wl_data_source_offer(gClipWlSource, "text/plain;charset=utf-8");
	wl_data_source_offer(gClipWlSource, "text/plain");
	wl_data_device_set_selection(gClipWlDevice, gClipWlSource, 0);
	wl_display_flush(dpy);
	return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool LinuxClipboardGetText(std::wstring &aText)
{
	aText.clear();
	if (Display *d = LinuxX11Display())
	{
		if (LinuxClipboardX11Read(d, aText))
			return true;
		// X11 read failed (no owner support); fall through to the
		// process-internal store so ClipWait keeps working headless.
	}
	else if (LinuxWaylandActive())
	{
		if (LinuxClipboardWaylandRead(LinuxWaylandDisplay(), aText))
			return true;
	}
	aText = LinuxClipboardFallback();
	return true;
}

bool LinuxClipboardSetText(const std::wstring &aText)
{
	LinuxClipboardFallback() = aText;
	if (Display *d = LinuxX11Display())
		return LinuxClipboardX11Write(d, aText);
	else if (LinuxWaylandActive())
		return LinuxClipboardWaylandWrite(LinuxWaylandDisplay(), aText);
	return true;
}

// ---------------------------------------------------------------------------
// P2-6 public snapshot API (ClipboardAll)
// ---------------------------------------------------------------------------

// Read ALL representations of the current clipboard into an AHKCB1 blob.
// The text entry is synthesized from the normal text read (X11/Wayland/
// fallback) so every backend contributes at least that; extra TARGETS
// atoms are requested one per MIME from the X11 owner when present.
bool LinuxClipboardGetAll(std::vector<unsigned char> &aOut)
{
	aOut.clear();
	std::wstring text;
	LinuxClipboardGetText(text);
	LinuxClipboardSnapshot snap;
	snap.owner_generation = gClipOwnerGen;
	if (!text.empty())
		LinuxClipSnapshotEnsureText(snap, text);
	// X11: walk TARGETS for non-text representations.
	if (Display *d = LinuxX11Display())
	{
		LinuxClipX11Ensure(d);
		if (XGetSelectionOwner(d, gClipX11Clipboard))
		{
			std::vector<unsigned char> raw;
			if (LinuxClipX11RequestTarget(d, gClipX11Targets, raw)
				&& raw.size() % sizeof(unsigned long) == 0)
			{
				size_t n = raw.size() / sizeof(unsigned long);
				unsigned long *atoms = (unsigned long *)raw.data();
				for (size_t i = 0; i < n; ++i)
				{
					Atom a = (Atom)(atoms[i] & 0xFFFFFFFFUL);
					if (a == gClipX11Targets || a == gClipX11Utf8
						|| a == XA_STRING || !a)
						continue;
					char *name = XGetAtomName(d, a);
					if (!name)
						continue;
					std::string mime(name);
					XFree(name);
					bool is_text = mime == "UTF8_STRING"
						|| mime == "STRING" || mime == "TEXT"
						|| mime == "COMPOUND_TEXT"
						|| mime == "text/plain;charset=utf-8"
						|| mime == "text/plain";
					if (is_text)
						continue; // Covered by the synthesized text entry.
					std::vector<unsigned char> data;
					if (LinuxClipX11RequestTarget(d, a, data)
						&& data.size() <= AHK_CB_MAX_ITEM_BYTES)
					{
						LinuxClipboardItem item;
						item.mime = mime;
						item.data = std::move(data);
						snap.items.push_back(std::move(item));
					}
				}
			}
		}
	}
	// Empty is serialized as a valid zero-item AHKCB1 container (12 bytes),
	// never as a zero-length blob: A_Clipboard := ClipboardAll() must be able
	// to restore an empty clipboard without being mistaken for corruption.
	std::vector<unsigned char> blob;
	if (!LinuxClipSnapshotSerialize(snap, blob))
	{
		return false; // Over the audit's total bound: report failure.
	}
	gClipSavedAll = snap;
	aOut = std::move(blob);
	return true;
}

// Restore an AHKCB1 blob: re-offer every representation.  On X11 we take
// ownership and serve each atom from the restored snapshot; the text entry
// also fills the plain-text path (A_Clipboard reads it back).
bool LinuxClipboardSetAll(const unsigned char *aData, size_t aSize)
{
	LinuxClipboardSnapshot snap;
	if (!LinuxClipSnapshotDeserialize(aData, aSize, snap))
	{
		return false;
	}
	snap.owner_generation = ++gClipOwnerGen;
	// The text representation drives the plain-text path.
	std::wstring text;
	for (auto &item : snap.items)
		if (item.mime == "text/plain;charset=utf-8"
			|| item.mime == "text/plain")
		{
			std::string bytes(item.data.begin(), item.data.end());
			std::wstring decoded;
			if (!LinuxUtf8ToWide(bytes, decoded))
				return false;
			text = std::move(decoded);
			break;
		}
	// Keep the process-internal store in sync no matter which system
	// backend (if any) takes the offer, exactly like LinuxClipboardSetText.
	LinuxClipboardFallback() = text;
	if (Display *d = LinuxX11Display())
	{
		LinuxClipX11Ensure(d);
		gClipSnapshot = std::move(snap);
		gClipX11Data = text;
		XSetSelectionOwner(d, gClipX11Clipboard, gClipX11Window, CurrentTime);
		XFlush(d);
		gClipX11Owned = (XGetSelectionOwner(d, gClipX11Clipboard)
			== gClipX11Window);
		return true;
	}
	if (LinuxWaylandActive())
		return LinuxClipboardWaylandWrite(LinuxWaylandDisplay(), text);
	return true;
}

// Owner-generation compare for the paste-restore path (audit §18).
uint64_t LinuxClipboardOwnerGeneration()
{
	return gClipOwnerGen;
}

// ---------------------------------------------------------------------------
// Clipboard-paste fallback transaction (pure-Wayland Send path)
// ---------------------------------------------------------------------------
// Lifecycle: the Send engine saves the existing text into PasteSet (together
// with the pasted text), injects Ctrl+V and calls PasteWaitConsumed; the
// original is restored with PasteRestore.  The restore happens once the
// target app has ACTUALLY requested our data offer (LinuxClipWlSourceSend /
// LinuxClipboardX11ServeRequest arm the flag), instead of a bare usleep, and
// an originally-empty clipboard comes back empty (check0820 P1).

static std::wstring sPasteOriginal; // Clipboard text saved at PasteSet time.
// Audit §18 concurrent-user-copy guard: PasteRestore re-offers the original
// ONLY when this process still owns the clipboard.  If a user (or another
// app) copied new content while the paste text was installed, the user's
// copy wins and the old original is abandoned -- restoring it would destroy
// the user's new clipboard.  Ownership is re-queried per backend in
// PasteRestore: the in-process gClipOwnerGen only tracks OUR writes and
// cannot see a foreign takeover.  On a headless fallback clipboard no
// foreign owner can exist, so restore is unconditional there.

// Does the active backend still report OUR window as the clipboard owner?
bool LinuxClipPasteStillOurs()
{
	Display *d = LinuxX11Display();
	if (d)
	{
		LinuxClipX11Ensure(d);
		Window owner = XGetSelectionOwner(d, gClipX11Clipboard);
		return owner == gClipX11Window; // Our hidden window still owns it.
	}
	if (LinuxWaylandActive())
		return LinuxClipWlOwnsSelection();
	return true; // Headless fallback: no foreign owner can exist.
}

bool LinuxClipboardPasteSet(const std::wstring &aText, const std::wstring &aSaved)
{
	sPasteActive = true;
	sPasteServed = false;
	sPasteOwnershipLost = false;
	sPasteOriginal = aSaved;
	if (!LinuxClipboardSetText(aText))
	{
		sPasteActive = false;
		return false;
	}
	return true;
}

// Wait for the target to actually request the pasted data (bounded); the
// caller restores after the deadline regardless (the paste may still land).
bool LinuxClipboardPasteWaitConsumed(int aTimeoutMs)
{
	if (sPasteServed || sPasteOwnershipLost)
		return sPasteServed;
	if (LinuxWaylandActive())
	{
		if (wl_display *dpy = LinuxWaylandDisplay())
		{
			LinuxClipWlWait(dpy, sPasteServed, aTimeoutMs);
			return sPasteServed;
		}
	}
	int waited = 0;
	while (sPasteActive && !sPasteServed && !sPasteOwnershipLost
		&& waited < aTimeoutMs)
	{
		// X11 SelectionRequest/SelectionClear callbacks are queued on the
		// same connection; dispatch them during the transaction as well.
		if (Display *d = LinuxX11Display())
			LinuxClipboardDispatchX11(d);
		if (sPasteServed || sPasteOwnershipLost)
			break;
		usleep(5000);
		waited += 5;
	}
	return sPasteServed;
}

void LinuxClipboardPasteRestore(bool aHadText)
{
	sPasteActive = false;
	// Compare-and-swap (audit §18): restore only when this transaction still
	// owns the clipboard.  A concurrent user copy transferred selection
	// ownership to another window while the paste text was installed; the
	// user's new copy wins and our original is abandoned (restoring it would
	// destroy the user's content).
	if (!LinuxClipPasteStillOurs())
	{
		static bool sPasteClobberWarned = false;
		if (!sPasteClobberWarned)
		{
			fprintf(stderr,
				"AHK info: paste-fallback restore skipped — the clipboard "
				"was replaced by another owner while the paste text was "
				"installed; the user's new copy is preserved.\n");
			sPasteClobberWarned = true;
		}
		return;
	}
	if (aHadText)
		LinuxClipboardSetText(sPasteOriginal);
	else
		LinuxClipboardSetText(L""); // Was empty: do not leave the sentinel.
}

