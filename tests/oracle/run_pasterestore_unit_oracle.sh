#!/bin/bash
# Paste-restore CAS unit oracle (check_detail0901 §18 phase 2): drives the
# LinuxClipboardPasteSet/Restore transaction directly against X11 where an
# independent xclip_probe client plays the concurrent user.  Verifies:
#   1. happy path restore brings back the saved original;
#   2. a foreign takeover while the paste text is installed makes the CAS
#      restore a no-op (the user's USER-COPY survives; ORIGINAL never comes
#      back).
# Runs under a dedicated Xvfb display.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
BIN="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
BUILD_ONLY=0
[ "${2:-}" = "--build-only" ] && BUILD_ONLY=1
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT"

command -v pkg-config >/dev/null || { echo PR_SKIP pkg-config; exit 2; }
command -v Xvfb >/dev/null || { echo PR_SKIP no-xvfb; exit 2; }
# The xclip_probe binary is normally built by the doccheck runner; this
# oracle may run before that step, so compile it into the oracle out dir
# from the shared single-file source (same code the doccheck uses).
PROBE="$OUT/xclip_probe"
${CC:-gcc} -O2 -Wall -o "$PROBE" "$ROOT/tests/doccheck/xclip_probe.c" \
	$(pkg-config --cflags --libs x11) || { echo PR_FAIL probe-build; exit 1; }

XVFB_DISPLAY=":63"
XVFB_PID=""
cleanup() {
	[ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
	pkill -f "xclip_probe" 2>/dev/null
	return 0
}
trap cleanup EXIT

if command -v Xvfb >/dev/null; then
	Xvfb "$XVFB_DISPLAY" -screen 0 1024x768x24 >/dev/null 2>&1 &
	XVFB_PID=$!
	sleep 2
fi
DISPLAY="$XVFB_DISPLAY" xdpyinfo >/dev/null 2>&1 || { echo PR_SKIP no-x; exit 2; }

# Build the unit binary against the repo's core objects.
UNIT="$OUT/pasterestore_unit"
CORE_INCS="-I$ROOT/source -I$ROOT/source/linux -I$ROOT/source/linux/core"
CLIPBOARD_OBJ="$ROOT/build-core/source/linux/core/CMakeFiles/ahk_core.dir/core_clipboard_linux.cpp.o"
# MsgMonitorList::Call shim (the dispatch hook references it; the paste unit
# never fires it).  Compiled against the repo headers like the clipboard TU.
cat > "$OUT/pasterestore_msgmon_shim.cpp" <<'EOF'
#include "script.h"
ResultType MsgMonitorList::Call(ExprTokenType *aParamValue, int aParamCount, int aInitNewThreadIndex, __int64 *aRetVal)
{
	return FAIL; // never called by the paste unit
}
EOF
g++ -O1 -g -std=gnu++17 -fpermissive -pthread -o "$UNIT" \
	"$ROOT/tests/oracle/pasterestore_unit.cpp" \
	"$ROOT/source/linux/core/core_clipboard_linux.cpp" \
	"$OUT/pasterestore_msgmon_shim.cpp" \
	$(pkg-config --cflags --libs x11 dbus-1 wayland-client gtk+-3.0 2>/dev/null) \
	-lXfixes \
	-I"$ROOT/source" -I"$ROOT/source/compat" \
	-I"$ROOT/source/linux/compat" \
	-I"$ROOT/build-core/source/linux/core/wl_gen" \
	-I"$ROOT/source/linux" 2>"$OUT/pasterestore_unit_link.log"
if [ ! -x "$UNIT" ]; then
	echo PR_SKIP link-failed
	sed -n '1,5p' "$OUT/pasterestore_unit_link.log"
	exit 2
fi

# --- Case 1: happy path (case 2 skipped) ---
rm -f "$OUT/pasterestore_unit.txt"
AHK_PR_CASE2=0 DISPLAY="$XVFB_DISPLAY" "$UNIT" > "$OUT/pasterestore_unit.txt" 2>&1
rc=$?
cat "$OUT/pasterestore_unit.txt"
[ "$rc" = 0 ] || { echo "PR_FAIL unit_rc=$rc"; exit 1; }
grep -q 'pasterestore_unit_PASS' "$OUT/pasterestore_unit.txt" \
	|| { echo PR_FAIL assertions; exit 1; }

# --- Case 2: foreign takeover during the paste transaction ---
# The unit's PasteSet takes ownership (installing the paste text); the
# harness then launches a foreign probe which steals it (the "user copy").
# The unit's poll must observe USER-COPY and the CAS restore must leave it
# untouched.
rm -f "$OUT/pr_own_b.txt" "$OUT/pasterestore_unit2.txt"
AHK_PR_CASE2=1 DISPLAY="$XVFB_DISPLAY" "$UNIT" > "$OUT/pasterestore_unit2.txt" 2>&1 &
UNIT_PID=$!
# The unit takes ~0.3 s for case 1; give it time to reach case 2's PasteSet
# (which installs UNIT-PASTED-2 and takes ownership), then steal.
sleep 0.8
DISPLAY="$XVFB_DISPLAY" "$PROBE" --set-mime \
	--mime "text/plain;charset=utf-8=555345522D434F5059" \
	--delay 2500 -out "$OUT/pr_own_b.txt" &
wait "$UNIT_PID"
cat "$OUT/pasterestore_unit2.txt"
grep -q '^case2_user_copy=1$' "$OUT/pasterestore_unit2.txt" \
	&& grep -q '^case2_user_survives=1$' "$OUT/pasterestore_unit2.txt" \
	|| { echo PR_FAIL takeover-case; exit 1; }

echo "PASTERESTORE_UNIT_PASS"
