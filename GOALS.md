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
- Layout-aware three-layer key model: active X11 keymap/state from
  xkbcommon-x11, canonical evdev/set-1 SC + VK/keysym + UTF-32, explicit scan
  code hotkeys on X11/evdev, and live-layout Shift/AltGr Send lookup.
- evdev custom-combo state machine: wildcard modifiers, up variants, tilde,
  VK/SC prefixes and delayed standalone-prefix behavior; X11 remains explicit
  unsupported until its raw combo state batch.
- XI2 raw capture: Hotstrings and visible InputHook are multi-client observers;
  original target events remain real, and replacement uses Backspace instead
  of all-key grabs/XSendEvent forwarding.
- InputHook selected suppression: exact KeyOpt/visibility keycodes are grabbed,
  raw handles all others, and runtime changes reconcile without restart.
- Versioned normalized input events: physical evdev/set-1 SC, VK, text,
  phase/repeat, source/SendLevel/device/origin across X11, evdev, Portal and
  GNOME entry points, with optional JSONL trace.
- Per-hotkey input mux: capability-based routing across X11/portal/GNOME/evdev
  with concurrent multi-lane registration and differential start/stop; exposed
  via HotkeyBackendGet().mux and --diag.
- ahk-inputd broker daemon: EVIOCGRAB capture, uinput replay, UNIX-socket v1
  multi-client subscription protocol with suppression arbitration, fail-open
  cleanup and watchdog/panic recovery (M4-D).
- Core broker client (M4-C + M4-C2): connect-first subscription, EVENT frames
  through the evdev matcher, in-process fallback, dual-script no-conflict
  acceptance, and broker character stream (X11 layout decode; pure Wayland
  needs a compositor layout source). libei receiver remains tracked follow-up.
- M5-A EWMH window enumeration: _NET_CLIENT_LIST preferred with ICCCM WM_STATE
  subtree-probe fallback that preserves unmanaged windows; dual-path oracle.
- M5-B Control* virtual-state retirement: external windows get a clear
  NotSupported for style/enabled/checked/Combo-List/Edit-caret/ListView-row
  instead of a fake process-local shadow; text operations stay real.
- M5-C AT-SPI WinTitle limiting: Control* on Wayland resolves WinTitle to its
  application (D-Bus destination) before searching, skipping app-root
  placeholders; two-process GTK oracle proves no cross-app matching.
- M5-C AT-SPI bulk cache/budget: one Cache.GetItems call per app (500ms),
  per-app GetChildren fallback, 2s total refresh deadline with partial results;
  diagnostics and VM oracles prove cache, fallback and 1ms timeout injection.
- Machine totals: 1135/1135 X11/headless, 17/17 Wayland and 230/230
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
