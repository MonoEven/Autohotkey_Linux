# External input oracle

These tests deliberately keep observation/injection outside `ahk_core` so the
port cannot validate one internal convention with another copy of itself.

`input_oracle.c` has two X11 modes:

- `record-x11`: an XI2.1 raw-event recorder that writes schema-1 JSONL with
  monotonic time, X server time, press/release, keycode, device/source IDs and
  an independent XTEST-device classification;
- `inject-x11`: a separate XTEST client used to drive an AHK hotkey.

`run_x11_oracle.sh` verifies both directions under Xvfb. The checked-in
`verify_trace.py` requires an exact down/up pair and an XI2 source ID belonging
to an XTEST device. Raw traces are test output; the stable schema and summary
make later Linux/Windows differential traces possible without changing the
consumer format.

`run_keymodel_oracle.sh` installs a checked-in, X11-compatible AZERTY/AltGr
fixture (maximum keycode 255) into an Xvfb server started with `-noreset`. It
proves that `sc01E` follows one physical key across logical-layout changes and
that `SendText("€")` reaches an independent focused X11 receiver as EuroSign
with Mod5/AltGr. The fixture avoids a known xkeyboard-config/X11 trap: modern
maps contain keycodes above 255, which xkbcomp may clip while leaving Xvfb on
its US map.

`run_input_event_model_oracle.sh` validates the version-1 normalized event
schema independently of the matching code. It requires exact physical
(evdev/set-1 SC), logical VK, text, phase, source, SendLevel, device and origin
values for both self Send and another XTEST process. The same schema is emitted
by evdev and shortcut entry points when `AHK_INPUT_EVENT_TRACE` is set.

`run_inputd_oracle.sh` proves the broker itself (distribution, replay,
suppression arbitration, client-crash cleanup, grab recovery),
`run_inputd_client_oracle.sh` proves the core client (two ahk_core scripts
share one broker stream and both fire combos without BadAccess; in-process
fallback when no broker exists) and `run_inputd_charstream_oracle.sh` proves
the broker character stream (a Hotstring fires from broker-distributed uinput
events decoded through the X11 layout). All three need root for EVIOCGRAB, so
they run on the VM rather than GitHub-hosted runners.
`run_inputd_socket_activation_oracle.sh` is the packaging/lifecycle counterpart:
its unprivileged CI lane uses the external `systemd-socket-activate` launcher to
prove LISTEN_FDS adoption, fragmented/coalesced protocol frames, SO_PEERCRED logs
and socket ownership; `AHK_SYSTEMD_REAL=1` installs temporary runtime units and
proves on-demand start, SIGKILL restart, five-second idle grab release, stable
socket inode, demand restart and complete cleanup.
`run_inputd_packaged_service_oracle.sh` then installs/uses the actual package
units and `/usr/bin/ahk`: with no socket override the core auto-discovers
`/run/ahk-inputd.sock`, enters broker mode twice across idle exits, preserves
socket identity, releases physical grabs and leaves no unit/socket after package
removal. `run_inputd_reconnect_oracle.sh` holds one AHK PID across a packaged
broker SIGKILL, requires the bounded 500ms/5s reconnect and rule resubscription,
and proves independent uinput F12 events fire both before and after the daemon
PID changes.

`run_atspi_wintitle_oracle.sh` launches two independent GTK applications with
same-named controls and proves WinTitle confines lookup to the owning app.
`run_atspi_cache_oracle.sh` then proves Cache.GetItems, an explicitly forced
per-app GetChildren fallback and a 1ms total-budget fault injection (bounded
partial return) while preserving UTF-8 text. These require a real GNOME
Wayland accessibility session and run on
the VM; the scenario `atspi_matrix` additionally requires a non-zero cache
count before it may pass. `run_controlsend_mode_oracle.sh` proves writable
focus/atspi mode selection, Unicode EditableText updates without DISPLAY and
explicit rejection of complex Send syntax.

`run_dbgp_oracle.sh` starts an independent Python DBGp IDE server and proves
protocol framing, init negotiation, a file-URI line breakpoint, run/step,
stack/context/scalar properties, paged Array/Map/nested Object trees, a caught
exception stop/Message/continue, then a bounded persistent-idle Pause with zero
real frames and readable Global state. It also expands a D-Bus proxy and typed
ComValue, asserting only side-effect-free local metadata. The final lanes detach,
reconnect the same PID twice around an ungraceful IDE socket close, and prove a
separate tight-running script services the signal from its execution hook. `run_dap_adapter_oracle.sh` drives the inline adapter as an external DAP
client over that real runtime. `run_vscode_extension_oracle.sh` installs the packaged 0.2.1 VSIX into VS Code
and proves extension-host activation, language/command registration, `--diag`
parsing and real script execution, then observes breakpoint/step/scalar variables
and termination through VS Code's DebugAdapterTracker. The repeatable VM visual smoke
flow and screenshot evidence are recorded in [VSCODE_VM_VISUAL.md](VSCODE_VM_VISUAL.md).
`run_gui_host_matrix.sh` then aggregates
independent GTK3, Qt6 and VS Code/Electron capture oracles. The Qt lane also
uses QListWidget/QSlider callbacks to prove AT-SPI Selection and Value produce
real toolkit changes, then fault-injects timeout/bus/interface/input failures to
verify A_LastError errno 0/2/22/61/95/107/110. A delayed pending reply proves a
Timer fires while ControlGetText waits; nested AT-SPI returns EBUSY and the outer
call remains successful. Cache/fallback/budget dumps require non-zero pending
call/pump-slice counts; see `GUI_HOST_MATRIX.md` for versions and the explicit
Monaco-content limitation.

`run_compositor_stop_oracle.sh` owns a private headless sway, establishes a
virtual-keyboard path, SIGSTOPs the compositor for three seconds and requires
30+ Timer/input iterations while stopped. After SIGCONT the same AHK PID must
trigger another sway binding and sway IPC must respond.

`run_evdev_hotplug_oracle.sh` is the privileged VM fault lane. An independent
uinput keyboard is added, fires a physical SC hotkey and is destroyed three
times while one runtime remains alive. `/proc/$pid/fd` independently requires
each transition to return to baseline, proving no stale event descriptors. Two
cycles remove the device with a custom-combo prefix held and require the next
keyboard to have no ghost prefix state.

`run_java_atspi_oracle.sh` drives a real OpenJDK Swing JFrame through Java ATK
Wrapper. Text, Action and Selection must change toolkit marker files. Java's
advertised readwrite Value property silently ignores Set, so runtime readback
must convert it to explicit EIO instead of success. `run_libreoffice_atspi_oracle.sh`
drives the real CSV Import OK action, then proves one-app WinTitle cache and
Calc's 1,048,576 by 16,384 Table metadata; virtual cells are an explicit API gap.

`run_mixed_soak.sh` runs one parameterized hotkey/Hotstring/clipboard/Timer
workload. CI uses 30 seconds; `AHK_SOAK_SECONDS=86400` remains an optional manual
profile, but the scheduled 24-hour run was cancelled by user request. It gates
exact event counts and bounded clipboard convergence, samples
warm RSS and emits a slope summary. Native builds gate warm RSS at 20MiB; ASan
still gates all event counts but records (does not gate) its intentional quarantine
RSS ramp. The current VM evidence is 1408 matching
rounds in five minutes with +148KB warm RSS; this is not a 24-hour claim.

`run_portal_restart_oracle.sh` starts an independent GlobalShortcuts portal twice
on one private session bus. It proves one unchanged AHK process receives an
activation, survives the owner disappearance, recreates/rebinds its session to
the replacement owner, and receives a second activation with the same shortcut
id. This is the CI fault-injection lane for portal restarts.

`run_ibus_ime_oracle.sh` is the real GNOME/XWayland IME lane: a GTK Entry with
GTK's IBus module receives XTEST `nihao+space`; an independent AHK process sees
cross-context preedit/commit scoped by FocusIn, fires a Chinese Hotstring, rejects
an ASCII preedit-decoy, and proves cancel/Backspace do not pollute InputHook,
OnChar or target text. A direct digit proves the 500ms no-preedit fallback. `run_fcitx5_ime_protocol_oracle.sh`
runs in CI with an independent D-Bus service and is explicitly protocol-only,
not a substitute for a real Fcitx5 desktop.

The physical-layer VM lane uses `tools/linux/uinput-inject.c`: its virtual
keyboard deliberately has no AHK identity, so evdev sees an independent input
device. It remains permission-gated and is not pretended to run on ordinary
GitHub-hosted runners.

Run locally:

```bash
xvfb-run -a bash tests/oracle/run_x11_oracle.sh \
  build-core/source/linux/core/ahk_core
```
