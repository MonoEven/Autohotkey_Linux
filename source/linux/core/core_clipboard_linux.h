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
