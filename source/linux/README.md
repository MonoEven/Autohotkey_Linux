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

Key runtime controls:

```text
AHK_INPUT_BACKEND=auto|x11|portal|gnome-shell|evdev
AHK_WAYLAND_PASTE=0|1
AHK_UINPUT=0|1
```

The exact capability matrix is maintained in
`docs-v2/docs/linux-port.htm`; machine-verifiable results are in
`tests/doccheck/CHECK_REPORT.md` and `tests/scenarios/`.
