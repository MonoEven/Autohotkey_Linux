// Declarations of the Linux system-clipboard layer (implemented in
// core_clipboard_linux.cpp): X11 CLIPBOARD selection on X11/XWayland,
// wl_data_device on pure Wayland, process-internal fallback otherwise.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

// --- P2-6 rich ClipboardAll (check_detail0901 §18) ----------------------
//
// Snapshot API over the AHKCB1 container: magic u32, version u32, count
// u32, then per item {mime_len u16, mime bytes, data_len u32, fnv1a
// checksum u32, data bytes}.  Per-item and total size bounds are enforced
// (32 MiB / 128 MiB); a corrupt or over-bound blob is rejected rather than
// partially restored.
//
// Read every representation of the current clipboard (text is synthesized
// from the normal text read; extra X11 TARGETS atoms are requested per
// MIME).  Returns false on a bound violation; an empty clipboard yields a
// valid empty blob.
bool LinuxClipboardGetAll(std::vector<unsigned char> &aOut);

// Restore a snapshot: re-offer every representation (X11: take ownership
// and serve each atom; the text entry also feeds the plain-text path).
// Returns false when the blob is not a valid AHKCB1 container.
bool LinuxClipboardSetAll(const unsigned char *aData, size_t aSize);

// Monotonic clipboard owner generation (bumps on every write; used by the
// paste-fallback restore to compare ownership per the audit).
uint64_t LinuxClipboardOwnerGeneration();

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

// Dispatch X11 clipboard events (SelectionRequest/SelectionClear /
// XFixes selection changes) while the script waits.  Called from the main
// loop and MsgSleep; a no-op unless this process owns the CLIPBOARD
// selection or a clipboard-change watch is active.
void LinuxClipboardDispatchX11(Display *d);

// --- Clipboard-change notification (OnClipboardChange, check_detail0821
// --- §4) ---------------------------------------------------------------
//
// Windows semantics: a script registering an OnClipboardChange callback is
// notified whenever the clipboard content changes, with a Type argument
// (0 = empty, 1 = text, 2 = non-text).  On X11/XWayland this is backed by
// XFixes selection tracking (XFixesSetSelectionOwnerNotifyMask on the
// CLIPBOARD selection); the event is detected in
// LinuxClipboardDispatchX11() and the callback is invoked synchronously
// from the main dispatch hook (matching where Windows delivers it: while
// the script is at a message-pump point, not preempting a running thread).
//
// AddClipboardFormatListener/RemoveClipboardFormatListener in the Win32
// shim header forward to these, so upstream Script::EnableClipboardListener
// wires the whole chain with no upstream change.
bool LinuxClipboardWatchStart();
bool LinuxClipboardWatchStop();
// True while a clipboard-change watch is active (any backend).
bool LinuxClipboardWatchActive();

// Dispatch Wayland data-device events (data_source send requests, data
// offer announcements).  Called from the Wayland dispatch hook.
void LinuxClipboardDispatchWayland();

// Wayland integration hooks (called from core_wayland_linux.cpp): bind
// wl_data_device_manager from the registry, and create the data device
// once the seat is available.
void LinuxClipboardWaylandRegistry(void *aData, wl_registry *aReg, uint32_t aName
	, const char *aIface, uint32_t aVersion);
void LinuxClipboardWaylandSeat(wl_seat *aSeat);
// M7 (§9.2): drop the current generation's data-device proxies when the
// Wayland connection is torn down; the next connect re-binds them.
void LinuxClipboardWaylandTeardown();
