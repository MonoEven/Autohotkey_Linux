# AutoHotkey v2 Linux port — current goals

Baseline: AutoHotkey v2.0.26 · branch: `linux-port` · release:
`v2.0.26-linux.16` · project status: **technology preview**.

This file is the current engineering status, not a chronological diary.
Release history is in `docs-v2/docs/ChangeLog.htm`; test evidence is in
`tests/doccheck/CHECK_REPORT.md` and `tests/scenarios/`.

## Delivered

### Interpreter and X11/XWayland

- AutoHotkey v2 language/runtime builds and runs natively on Linux.
- X11/XWayland window, control, input, clipboard, pixel, monitor, dialog,
  hotkey, hotstring, InputHook and GTK3 GUI/Menu backends.
- Unicode Send/hotstring input, including serialized temporary X keymap
  borrowing across processes.
- Cross-process XGrabKey conflict reporting and non-blocking Linux hotkey
  throttle warnings.

### Native Wayland and global input

- xdg-shell, wlroots virtual keyboard/pointer and screencopy.
- Backends: X11, XDG Global Shortcuts portal, GNOME Shell extension and
  evdev/uinput; runtime caps and per-key routing via
  `A_HotkeyBackend`/`HotkeyBackendGet()`.
- wlroots private-keymap Unicode injection (wtype model) with default-keymap
  restoration.
- evdev/uinput remap chain, CapsLock dual-role reference configuration and
  Backspace→Escape→Enter panic release for EVIOCGRAB.

### Desktop and interoperability

- GTK3 GUI, controls and menus.
- AT-SPI controls on GNOME Wayland; GNOME Terminal and Firefox matrix tested.
- TrayTip through desktop notifications; TraySetIcon/A_TrayMenu through
  StatusNotifierItem/dbusmenu with AutoHotkey's official tray icon.
- `.so` DllCall + libffi callbacks, including Float/Double ABI types.
- D-Bus-adapted COM layer and IBus coexistence.

### Packaging and quality gates

- deb, RPM, tarball, AppImage, AUR PKGBUILD and Flatpak manifest.
- `ahk_core --pack`, `A_IsCompiled` and embedded FileInstall resources.
- CI: regular + ASan, scenario gate, no-XWayland, packed-binary acceptance,
  RSS/event-count soak and fedora/arch/debian/ubuntu container builds.
- External input oracle: independent XI2.1 JSONL recorder and XTEST injector
  gate both Send output and hotkey input directions; uinput covers the VM
  physical-device lane.
- Machine totals: 1152/1152 X11/headless, 17/17 Wayland and 255/255
  XWayland assertions; 27/27 headless regression tests.

## Active constraints / remaining work

These are not marked complete without a matching environment and automated
result:

- Full multi-client `ahk-inputd` system service (the remap core and panic key
  exist; daemon arbitration/systemd ownership remain).
- Dedicated AutoHotkey IBus engine (coexistence is tested; no engine yet).
- KDE VM end-to-end matrix and a real Flatpak-host run.
- Wider AT-SPI application matrix (currently GNOME Terminal + Firefox; evince
  needs a real document fixture, and LibreOffice/Qt/Electron/Java hosts are not
  installed in the current VM).
- libei injection, unless a compositor/portal environment suitable for E2E
  verification is provided.
- Supplementary-plane Wayland text (UTF-16 surrogate-pair/emoji combination);
  BMP Unicode is tested.

## Verification commands

```bash
cmake -S . -B build-core
cmake --build build-core -j2
bash tests/run_tests.sh build-core/source/linux/core/ahk_core
bash tests/doccheck/run_check.sh --xvfb build-core/source/linux/core/ahk_core
bash tests/doccheck/wayland_run.sh build-core/source/linux/core/ahk_core
bash tests/scenarios/run_scenarios.sh build-core/source/linux/core/ahk_core --env x11
```

The detailed parity/capability matrix is
`docs-v2/docs/linux-port.htm`; roadmap claims are audited in
`audits/check_detail0821.md` against code, VM tests and CI results.
