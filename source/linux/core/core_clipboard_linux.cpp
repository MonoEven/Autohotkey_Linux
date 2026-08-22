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

static std::wstring LinuxUtf8ToWide(const std::string &aUtf8)
{
	std::wstring out;
	size_t i = 0;
	while (i < aUtf8.size())
	{
		unsigned char c = (unsigned char)aUtf8[i];
		unsigned int cp = 0;
		int extra = 0;
		if (c < 0x80) { cp = c; }
		else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
		else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
		else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
		else { ++i; continue; }
		if (i + extra >= aUtf8.size())
			break;
		bool ok = true;
		for (int k = 1; k <= extra; ++k)
		{
			unsigned char cc = (unsigned char)aUtf8[i + k];
			if ((cc & 0xC0) != 0x80) { ok = false; break; }
			cp = (cp << 6) | (cc & 0x3F);
		}
		if (!ok) { ++i; continue; }
		out += (wchar_t)cp;
		i += extra + 1;
	}
	return out;
}

// ---------------------------------------------------------------------------
// X11 CLIPBOARD selection
// ---------------------------------------------------------------------------

// Paste transaction flags (check0820 P1): shared by the Wayland source-send
// handler and the X11 SelectionRequest handler, which both run before the
// public paste API below.  File scope so either handler can observe them.
static bool sPasteActive = false;
static bool sPasteServed = false;

namespace {

Display *gClipX11Display = nullptr;
Window gClipX11Window = 0;        // Hidden window: ownership + event target.
Atom gClipX11Utf8 = 0;
Atom gClipX11Prop = 0;            // Property name used for transfers.
Atom gClipX11Clipboard = 0;       // CLIPBOARD selection atom.
Atom gClipX11Targets = 0;         // TARGETS atom.
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
	gClipX11TextAtom = gClipX11Utf8;
}

} // namespace

// Forward declaration (defined below; used by the read path).
static void LinuxClipX11ServeRequest(Display *d, XSelectionRequestEvent *req);

// Read: request the CLIPBOARD selection and wait (bounded) for the result.
static bool LinuxClipboardX11Read(Display *d, std::wstring &aText)
{
	aText.clear();
	LinuxClipX11Ensure(d);
	Window owner = XGetSelectionOwner(d, gClipX11Clipboard);
	if (!owner)
		return true; // Empty clipboard, no error.

	// Request the selection as UTF8_STRING into our property.  We must NOT
	// take ownership here: XConvertSelection asks the current owner for
	// data (taking ownership would steal the selection from its owner).
	gClipX11Reading = true;
	gClipX11ReadDone = false;
	gClipX11ReadFailed = false;
	XConvertSelection(d, gClipX11Clipboard, gClipX11Utf8, gClipX11Prop, gClipX11Window, CurrentTime);
	XFlush(d);

	// Wait for SelectionNotify (bounded: AHK_CLIPBOARD_TIMEOUT_MS, default
	// 2 s - a slow owner may take a while to serve the request, check0820).
	time_t deadline = time(nullptr) + LinuxClipTimeoutMs() / 1000;
	if (LinuxClipTimeoutMs() % 1000)
		++deadline; // Round up partial seconds.
	while (!gClipX11ReadDone && !gClipX11ReadFailed && time(nullptr) < deadline)
	{
		struct pollfd pfd;
		pfd.fd = ConnectionNumber(d);
		pfd.events = POLLIN;
		pfd.revents = 0;
		if (poll(&pfd, 1, 20) > 0)
		{
			while (XPending(d) > 0)
			{
				XEvent ev;
				XNextEvent(d, &ev);
				if (ev.type == SelectionNotify && ev.xselection.requestor == gClipX11Window)
				{
					gClipX11ReadDone = true;
					if (ev.xselection.property == None)
					{
						gClipX11ReadFailed = true; // Owner declined.
						break;
					}
					// Read the property.
					Atom type;
					int format;
					unsigned long nitems, after;
					unsigned char *data = nullptr;
					if (XGetWindowProperty(d, gClipX11Window, gClipX11Prop, 0, 0x7fffffff,
						True, AnyPropertyType, &type, &format, &nitems, &after, &data) == Success
						&& data)
					{
						std::string utf8((const char *)data, nitems);
						aText = LinuxUtf8ToWide(utf8);
						XFree(data);
					}
					else
						gClipX11ReadFailed = true;
					break;
				}
				// SelectionRequest while reading: we own the selection (a
				// self-conversion or a foreign request); serve it directly
				// -- the event has already been dequeued.
				if (ev.type == SelectionRequest)
					LinuxClipX11ServeRequest(d, &ev.xselectionrequest);
			}
		}
	}
	gClipX11Reading = false;
	if (gClipX11ReadFailed)
	{
		// Fall back to STRING if the owner only offers that.
		// (Retried only when we did not get any SelectionNotify at all.)
		return false;
	}
	return true;
}

// Write: take ownership; serve SelectionRequest events later.
static bool LinuxClipboardX11Write(Display *d, const std::wstring &aText)
{
	LinuxClipX11Ensure(d);
	gClipX11Data = aText;
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
		Atom targets[] = { gClipX11Targets, XA_STRING, gClipX11Utf8 };
		XChangeProperty(d, req->requestor, req->property, XA_ATOM, 32,
			PropModeReplace, (unsigned char *)targets, 3);
	}
	else if (req->target == gClipX11Utf8 || req->target == XA_STRING)
	{
		XChangeProperty(d, req->requestor, req->property, req->target, 8,
			PropModeReplace, (unsigned char *)utf8.data(), (int)utf8.size());
	}
	else
	{
		sel.property = None;
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
	if (aFd >= 0)
	{
		size_t off = 0;
		while (off < utf8.size())
		{
			ssize_t n = write(aFd, utf8.data() + off, utf8.size() - off);
			if (n <= 0)
				break;
			off += (size_t)n;
		}
		close(aFd);
		if (sPasteActive)
			sPasteServed = true; // The app pulled our offer (the actual paste).
	}
}

void LinuxClipWlSourceCancelled(void *aData, wl_data_source *aSource)
{
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
				gClipWlOfferData.append(buf.data(), (size_t)n);
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
	aText = LinuxUtf8ToWide(gClipWlOfferData);
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
// Clipboard-paste fallback transaction (pure-Wayland Send path)
// ---------------------------------------------------------------------------
// Lifecycle: the Send engine saves the existing text into PasteSet (together
// with the pasted text), injects Ctrl+V and calls PasteWaitConsumed; the
// original is restored with PasteRestore.  The restore happens once the
// target app has ACTUALLY requested our data offer (LinuxClipWlSourceSend /
// LinuxClipboardX11ServeRequest arm the flag), instead of a bare usleep, and
// an originally-empty clipboard comes back empty (check0820 P1).

static std::wstring sPasteOriginal; // Clipboard text saved at PasteSet time.

bool LinuxClipboardPasteSet(const std::wstring &aText, const std::wstring &aSaved)
{
	sPasteActive = true;
	sPasteServed = false;
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
	if (sPasteServed)
		return true;
	int waited = 0;
	while (sPasteActive && !sPasteServed && waited < aTimeoutMs)
	{
		usleep(5000);
		waited += 5;
	}
	return sPasteServed;
}

void LinuxClipboardPasteRestore(bool aHadText)
{
	sPasteActive = false;
	if (aHadText)
		LinuxClipboardSetText(sPasteOriginal);
	else
		LinuxClipboardSetText(L""); // Was empty: do not leave the sentinel.
}
