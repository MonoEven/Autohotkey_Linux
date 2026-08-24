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
exception stop/Message/continue, detach and the resumed script's external result. `run_dap_adapter_oracle.sh` drives the inline adapter as an external DAP
client over that real runtime. `run_vscode_extension_oracle.sh` installs the
packaged 0.2.0 VSIX into VS Code
and proves extension-host activation, language/command registration, `--diag`
parsing and real script execution, then observes breakpoint/step/scalar variables
and termination through VS Code's DebugAdapterTracker. `run_gui_host_matrix.sh` then aggregates
independent GTK3, Qt6 and VS Code/Electron capture oracles; see
`GUI_HOST_MATRIX.md` for the exact versions and the explicit Monaco-content
limitation.

The physical-layer VM lane uses `tools/linux/uinput-inject.c`: its virtual
keyboard deliberately has no AHK identity, so evdev sees an independent input
device. It remains permission-gated and is not pretended to run on ordinary
GitHub-hosted runners.

Run locally:

```bash
xvfb-run -a bash tests/oracle/run_x11_oracle.sh \
  build-core/source/linux/core/ahk_core
```
