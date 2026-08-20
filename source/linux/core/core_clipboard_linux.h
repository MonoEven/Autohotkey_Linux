// Declarations of the Linux system-clipboard layer (implemented in
// core_clipboard_linux.cpp): X11 CLIPBOARD selection on X11/XWayland,
// wl_data_device on pure Wayland, process-internal fallback otherwise.
#pragma once

#include <string>

struct _XDisplay;
typedef struct _XDisplay Display;
struct wl_registry;
struct wl_seat;

// Get the current system clipboard text.  Returns true and fills aText on
// success (empty string when the clipboard is empty); false when the
// system clipboard is unreachable (no display backend).
bool LinuxClipboardGetText(std::wstring &aText);

// Set the system clipboard text.  Returns true on success (the data is
// owned and served on request); false when unreachable.
bool LinuxClipboardSetText(const std::wstring &aText);

// --- Clipboard-paste fallback transaction (pure-Wayland Send path) ---
//
// The paste fallback temporarily replaces the clipboard (set text, inject
// Ctrl+V, restore).  These three calls manage one transaction so the
// target application actually consumes the data before it is restored, and
// an empty original clipboard comes back empty (the pasted text never
// lingers):
//   PasteSet(aText, aSaved)  remember the original + set the paste text
//   PasteWaitConsumed(aMs)   wait for the target to request the offer
//   PasteRestore(aHadText)   restore aSaved (or clear when the clipboard
//                            originally had no text); falls back on timeout.
bool LinuxClipboardPasteSet(const std::wstring &aText, const std::wstring &aSaved);
bool LinuxClipboardPasteWaitConsumed(int aTimeoutMs);
void LinuxClipboardPasteRestore(bool aHadText);

// Dispatch X11 clipboard events (SelectionRequest/SelectionClear) while
// the script waits.  Called from the main loop and MsgSleep; a no-op
// unless this process owns the CLIPBOARD selection.
void LinuxClipboardDispatchX11(Display *d);

// Dispatch Wayland data-device events (data_source send requests, data
// offer announcements).  Called from the Wayland dispatch hook.
void LinuxClipboardDispatchWayland();

// Wayland integration hooks (called from core_wayland_linux.cpp): bind
// wl_data_device_manager from the registry, and create the data device
// once the seat is available.
void LinuxClipboardWaylandRegistry(void *aData, wl_registry *aReg, uint32_t aName
	, const char *aIface, uint32_t aVersion);
void LinuxClipboardWaylandSeat(wl_seat *aSeat);
