# AutoHotkey v2 Linux port — current goals

Baseline: AutoHotkey v2.0.26 · branch: `linux-port` · release:
`v2.0.26-linux.18` · project status: **technology preview**.

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
  cleanup and watchdog/panic recovery (M4-D). Deb/RPM and the opt-in tar
  installer now ship hardened systemd socket activation (`root:input 0660`),
  SO_PEERCRED audit logs, SIGKILL restart and five-second idle grab release.
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
- M5-C A_ControlSendMode: writable focus/atspi mode; AT-SPI appends plain Unicode
  to EditableText or triggers Enter/Space Action without X focus, rejecting
  unsupported key syntax explicitly; GNOME oracle + four CI contract assertions.
- M5-C AT-SPI Selection/Value: Wayland ControlChoose/Find/Get* use real Selection
  children; ControlGet/SetText maps scalar widgets to CurrentValue. Qt 6.9.2
  callbacks prove list/slider effects and unsupported/invalid cases error.
- M5-C A_LastError bridge: AT-SPI operations update per-thread Linux errno codes;
  Qt/fault oracles prove 0, ENOENT, EINVAL, ENODATA, ENOTSUP, ENOTCONN and ETIMEDOUT.
- M5-C pending-call integration: all 12 blocking libdbus calls were removed;
  bounded pending waits pump the Linux loop, Timer responsiveness is proven,
  nested AT-SPI returns EBUSY, and cache/fallback/budget counters are non-zero.
- M5 IME: ImeStatus reports engine/preedit/listener state; real IBus 1.5.32 +
  libpinyin commit `你好` drives Hotstring/InputHook after canceled-preedit rollback,
  with normalized ime_commit events. Fcitx5 protocol CI passes; real desktop E2E
  and Flatpak IM visibility remain explicit environment gaps.
- VSC-1/VSC-2 VS Code 0.2.0: syntax/run/tasks/diagnostics/capabilities plus
  Linux DBGp and inline DAP breakpoint/stack/scopes/scalars/stepping; independent
  protocol/DAP oracles and a real VS Code 1.134 extension-host tracker pass.
- GUI-1/M6 real host matrix: GTK 3.24.50 and Qt 6.9.2 full tested controls;
  OpenJDK 21/Java ATK 0.42.1 Text/Action/Selection pass with Value-write EIO;
  LibreOffice Calc 25.8.7 dialog Action/title-scoped Table metadata pass; VS Code/
  Electron 1.134.0 window/document is captured but Monaco content is unavailable.
- M6 TSan input gate: Debug/O0 race instrumentation runs 27 headless tests plus
  four independent X11/keymodel/raw/suppression oracles; package waits on it.
- M6 scenario-oracle identity gate: 23 scenarios are linted for exact owned
  evidence; D-Bus monitors must bind destination/object path. The SNI oracle now
  proves this AHK PID's bus instead of matching any StatusNotifierItem text.
- M6 portal restart fault gate: NameOwnerChanged rebuilds GlobalShortcuts state;
  an independent A/B portal proves the same AHK process recreates/rebinds and
  receives two activations. Session-bus reconnect retries are throttled to 500ms.
- M6 Windows differential gate (first slice): official v2.0.26 x64 archive/exe/
  trace hashes are pinned; three Windows runs are byte-identical. Linux's external
  XTEST replay exactly matches 20 VK/SC/Unicode/Hotkey/Hotstring rows (including
  wildcard, case-sensitive and inside-word negative/positive plus A_EndChar) and
  fixed five real parity defects. Physical-Windows pass-through, remap and wider option matrices remain open.
- M6 mixed soak: one parameterized hotkey/Hotstring/clipboard/Timer workload runs
  30s in CI and retains an optional 86400s manual profile. A 5-minute VM profile
  passed 1408 rounds; warm RSS grew 148KB (29.60KB/min). The 24h run was cancelled
  by user request and is not a linux.17 completion/release criterion.
- M6 evdev hotplug: zero-device rescans recover; removed device fds are ungrabbed,
  closed and pruned. Three uinput add/fire/remove cycles returned fd count 5→6→5
  with one stable runtime PID, no stale descriptor and no held-prefix ghost state.
- M6 compositor pause: private sway receives input before STOP, remains stopped 3s
  while the same AHK runs 30+ Timer/input ticks, then receives input after CONT and
  restores IPC; three runs pass and the runtime PID never changes.
- Complete examples: all 370 worklist IMPL functions map to generated pages and
  executable Linux evidence; 230 include upstream code. Curated headless/X11/
  lifecycle/safety profiles pass, CI rejects omissions/drift, and packages ship them.
- Machine totals: 1143/1143 X11/headless, 17/17 Wayland and 234/234
  XWayland assertions; 27/27 headless regression tests.

## Active constraints / remaining work

These are not marked complete without a matching environment and automated
result:

- Real Fcitx5 desktop and Flatpak/portal IM-context visibility (protocol path is
  CI-tested; the current GNOME VM uses IBus/libpinyin).
- KDE VM end-to-end matrix and a real Flatpak-host run.
- Wider AT-SPI matrix still needs real Java Value-write support, LibreOffice
  virtual-cell APIs and other real documents; all current limitations have
  versioned host evidence instead of inferred toolkit compatibility.
- VSC-2 D1-D3 complete for declared Linux scope: protocol/DAP, paged containers,
  exceptions, idle Pause, D-Bus metadata and same-PID detach/reconnect/crash
  cleanup pass raw DBGp, external DAP and real VS Code oracles.
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
