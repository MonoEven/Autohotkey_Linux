# Linux port source tree

This directory contains the Linux platform layer for the AutoHotkey v2.0.26
interpreter. The supported backend is selected at runtime; Windows project
files do not include these sources.

- `core/`: X11, native Wayland, portal, GNOME Shell and evdev/uinput input
  backends; window/control APIs; GTK3 GUI; AT-SPI; clipboard; D-Bus COM;
  libffi DllCall/callbacks; StatusNotifierItem tray; `--pack` payload support.
- `gui/`: GTK3/X11 dialogs, GUI controls, menus and Linux MessageBox handling.
- `compat/`: Win32/TCHAR/ABI compatibility used by the shared v2 runtime.
- `protocols/`: generated Wayland protocol bindings.

The input capability contract is generated from `core/input_caps.def`.
`HotkeyBackendGet()` and `ahk_core --diag` expose schema version 2; generated
documentation is checked by `tools/gen_input_caps.py --check`.
`core/core_keymodel_linux.*` is the canonical physical evdev/set-1 SC → logical
VK/keysym → Unicode-text model. X11 loads the active server map through
xkbcommon-x11; Send character lookup prefers the least-modified live-layout
binding before the Unicode borrow fallback. The evdev lane also owns the first
custom-combo state machine (`A & B`, up/tilde/wildcard and standalone-prefix
delay); other lanes keep `custom_combo=false`. `core_capture_linux.cpp` now
uses the existing XI2.1 raw subscription for Hotstrings and visible InputHook;
suppression is a selected passive-grab set derived from InputHook visibility and
KeyOpt; raw handles every unselected key and runtime changes reconcile it.
`core/input_event.*` is the versioned normalization/wire-schema precursor for
M3/M4: every lane reports physical/logical/text/source/level/device/origin, and
`AHK_INPUT_EVENT_TRACE=<path>` writes its JSONL oracle. `input_backend.*` owns
the per-hotkey mux: each hotkey is routed to the lane whose caps satisfy it, and
several lanes can stay active at once (see `--diag input-mux` /
`HotkeyBackendGet().mux`).

Key runtime controls:

```text
AHK_INPUT_BACKEND=auto|x11|portal|gnome-shell|evdev
AHK_WAYLAND_PASTE=0|1
AHK_UINPUT=0|1
```

The exact capability matrix is maintained in
`docs-v2/docs/linux-port.htm`; machine-verifiable results are in
`tests/doccheck/CHECK_REPORT.md` and `tests/scenarios/`.
