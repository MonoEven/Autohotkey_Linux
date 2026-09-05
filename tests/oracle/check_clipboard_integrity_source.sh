#!/bin/bash
# Static guard for ClipboardAll/paste integrity contracts (check0905 P2-6).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/source/linux/core/core_clipboard_linux.cpp"
grep -q 'gClipX11Incr' "$SRC"
grep -q 'PropertyChangeMask' "$SRC"
grep -q 'static bool LinuxUtf8ToWide' "$SRC"
grep -q 'if (!LinuxClipMimeSafe(item.mime))' "$SRC"
grep -q 'LinuxClipWlSourceCancelled' "$SRC"
grep -q 'LinuxClipWlWait(dpy, sPasteServed' "$SRC"
grep -q 'LinuxClipPasteStillOurs' "$SRC"
echo 'CLIPBOARD_INTEGRITY_STATIC_PASS incr=1 strict_utf8=1 mime_validation=1 wayland_cancel=1 dispatch_wait=1 cas=1'
