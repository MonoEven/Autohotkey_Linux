# Engineering audits

These files are dated review snapshots and the implementation roadmap derived
from them. They are kept separate from the current product documentation so
old counts and findings are not mistaken for the latest release status.

| File | Snapshot / purpose |
|---|---|
| [check0818.md](check0818.md) | 2026-08-17 hotkey/input audit |
| [check0819.md](check0819.md) | 2026-08-19 product-readiness audit |
| [check0820.md](check0820.md) | 2026-08-20 release/backend audit |
| [check0821.md](check0821.md) | 2026-08-21 comprehensive review |
| [check_detail0821.md](check_detail0821.md) | Detailed roadmap plus the current evidence-based completion audit |
| [check0824.md](check0824.md) | 2026-08-24 independent technical audit (commit `eaeeaf74`) |
| [check_detail0824.md](check_detail0824.md) | Detailed gap breakdown and per-item solution designs derived from check0824 |
| [check0901.md](check0901.md) | 2026-08-30 independent technical audit (commit `5bc26e5a`) |
| [check_detail0901.md](check_detail0901.md) | Per-item designs for every P0-P3 gap; milestone plan MF-M9 |

Closures since check0901 (evidence in the linked oracles):

- **P0-1 (inputd replay fail-open)** — replay device is created and
  validated before any grab; every `UI_SET_*`/`UI_DEV_CREATE` result is
  checked; replay writes fail-open (release all grabs + client-visible
  `BACKEND_DEGRADED` + `STATUS=degraded`); two-phase `EVIOCGKEY` grab with
  defer/retry. Fault oracle:
  [run_inputd_replay_failure_oracle.sh](../tests/oracle/run_inputd_replay_failure_oracle.sh)
  (12/12: EACCES, ENOTTY, runtime write failure, held-key defer/retry).
- **P0-2 / P1-3 (SendLevel/InputLevel + SendMode Input)** — unified
  policy entry in
  [input_semantics.h](../source/linux/core/input_semantics.h) with the
  official strict `send_level > input_level` rule; hotkey/hotstring threads
  start at their InputLevel; hotstring buffer excludes level 0; Send/SendText
  under SendMode "Input" and explicit SendInput share hook-unloaded
  semantics; SendPlay never fires own hooks. Policy oracle:
  [run_sendlevel_policy_oracle.sh](../tests/oracle/run_sendlevel_policy_oracle.sh)
  (22/22 documented Windows golden matrix). Scenario runner + mixed soak
  re-encoded to the same golden self-trigger path (SendMode Event +
  SendLevel 1); examples catalog regenerated accordingly.
- **P0-3 (protocol v2 — identity + authorization + provenance + broker-owned
  injection)** — v2 wire format on the same socket, discriminated by the
  `AHK2` magic (a v1 u32 length can never equal it); HELLO/HELLO_ACK
  negotiation with protocol range, capability grants (OBSERVE always;
  SUPPRESS/INJECT owner/root only; EXCLUSIVE/REMAP reserved for P1-4), and a
  per-process script nonce bound to a broker-assigned client_id; strictly
  monotonic client_seq; every broker-lane event carries an authoritative
  envelope (authority + generation + event_seq, source/confidence,
  send_level, producer/transaction ids); machine-readable ERROR codes; v1
  clients keep legacy semantics on the same socket; the client negotiates
  v2 first and falls back to v1 transparently.  M4a adds the broker-owned
  INJECT transaction protocol (BEGIN/EVENT/COMMIT/ABORT + per-stage ACKs)
  with TTL, preflight event-count bound, per-client/total quotas, crash
  auto-abort with held-key balancing, restart generation staleness, and a
  Windows-golden consumer level gate on broker-lane synthetic events
  (`send_level > input_level` fires).  Oracles:
  [run_inputd_v2_protocol_oracle.sh](../tests/oracle/run_inputd_v2_protocol_oracle.sh)
  (25/25),
  [run_inputd_injection_oracle.sh](../tests/oracle/run_inputd_injection_oracle.sh)
  (24/24) and the extended
  [client framing oracle](../tests/oracle/run_inputd_client_framing_oracle.sh).
- **P1-4 / M4b (static multi-client arbitration)** — typed registrations for
  OBSERVE/SUPPRESS/EXCLUSIVE/REMAP with registration_id, priority,
  broker acceptance sequence, lease, conflict policy, replacement key and
  replacement SendLevel; EXCLUSIVE/REMAP conflicts are deterministic,
  equal-priority ties keep the first accepted owner, and higher-priority
  PREEMPT_LOWER sends a machine-readable CONFLICT to the displaced owner.
  Disconnect/lease expiry releases ownership; replacement keys are
  neutralized; a suppressed physical down retains sticky key-up ownership.
  Every physical decision emits a DECISION trace and every capture gets a
  broker source transaction.  An independent target watcher reads the
  broker's uinput event node, proving OBSERVE replay, SUPPRESS zero-output,
  A/B/C single replacement, preemption, owner-crash/lease recovery and
  balanced key-up behavior. Oracle:
  [run_inputd_arbitration_oracle.sh](../tests/oracle/run_inputd_arbitration_oracle.sh)
  (29/29). Dynamic HotIf deadlines and logind active-seat ownership remain
  M5/M7 work; keyboard default remains fail-open.
- **M2a (unified backend health/generation/diagnostics + CompatibilityOutcome)**
  — static capabilities are separated from current `BackendHealth` and
  `RouteGuarantees`; the common registry rejects stale generation/health_seq
  updates and exposes state, local connection generation, broker authority
  generation, coverage, permission, replay, registration/held reconciliation,
  last errno/reason and compatibility outcome through `HotkeyBackendGet`.
  inputd v2 publishes typed BACKEND_HEALTH snapshots; the client transitions
  PROBING→BINDING→RESUBSCRIBING→RECONCILING_STATE→HEALTHY and preserves desired
  registrations across reconnect. Device held at the grab boundary stays
  reconciling until release/regrab; zero coverage cannot claim healthy; v1
  fallback is explicitly adapted rather than authoritative. Oracle:
  [run_input_backend_health_oracle.sh](../tests/oracle/run_input_backend_health_oracle.sh)
  (23/23: available/degraded/healthy, coverage, held-state recovery and the
  same AHK PID across broker SIGKILL/restart with generation++ and new
  authority). Portal/GNOME/Wayland service-owner generations remain M7.
- **P1-1 / M5a (normalized reducer + ordinary hotkey pipeline)** — adds a
  typed pipeline which keeps `InputEvent` facts, reducer snapshot,
  `InputDecision` and injection transactions separate.  Broker EVDEV,
  in-process EVDEV and X11 passive-grab ordinary hotkeys now pass through the
  same acceptance identity, generation-aware logical/physical reducer,
  consumer level policy, matcher, decision and dispatch code; callback
  threads inherit InputLevel as SendLevel uniformly. X11 adapter modifier
  snapshots cover modifiers not delivered as separate grabbed events.
  `GetKeyState(..., "P")`/KeyWait's compat shim reads the authoritative
  reducer for EVDEV; `HotkeyBackendGet` reports pipeline mode, state source and
  reducer generation. `AHK_INPUT_PIPELINE=active|mirror|legacy` provides a
  rollback/equivalence gate; JSONL trace links capture→reduce→match→decision→
  outcome→dispatch with backend/transaction identity. Oracle:
  [run_input_pipeline_oracle.sh](../tests/oracle/run_input_pipeline_oracle.sh)
  proves active/mirror/legacy callback equivalence on broker EVDEV and X11
  Ctrl+F8, authoritative physical GetKeyState, and zero mirror mismatch.
  Custom combo/remap suppression and Hotstring/InputHook callback migration
  remain M5b.
- **M5b-1 (wildcard/LR/key-up matcher + suppression decisions)** — the
  normalized reducer now retains left/right Ctrl/Shift/Alt/Win, adapter LR
  snapshots, and per-key key-up ownership. Broker clients subscribe modifier
  keys as observe-only state inputs. The common matcher implements the X11
  unique-resolution contract: exact beats wildcard; fewer consolidated side
  allowances wins; ties keep registration order; wrong-side LR is rejected.
  Key-up registrations claim the down phase, keep a balanced suppression pair,
  and fire only on release. The EVDEV legacy matcher independently implements
  the same scoring for mirror validation. Oracle:
  [run_input_pipeline_special_oracle.sh](../tests/oracle/run_input_pipeline_special_oracle.sh)
  runs wrong/correct-side Ctrl+F7, Shift exact-vs-wildcard F8, Alt extra-modifier
  wildcard, and F9-up through inputd and X11 in active/mirror/legacy modes,
  checking callback/SendLevel equality, decision traces and zero mirror
  mismatch. Broker dynamic HotIf suppression execution and custom combo/remap
  remain later M5b slices.
- **M5b-2 (InputHook/Hotstring normalized consumers)** — InputHook key/char
  callback queues and Hotstring raw/held match outcomes now consume a typed
  `AhkInputAcceptance` and emit consumer decisions/outcomes with the same
  authority/backend/transaction identity as hotkeys. Broker EVDEV enriches and
  reuses the hotkey adapter's acceptance (no duplicate reducer); X11 raw creates
  an explicit X11_RAW acceptance, while a selected suppression grab owns a
  separate X11_GRAB acceptance and raw records the handoff. InputHook
  MinSendLevel, SendInput/Play exclusion, queued/stale callback outcome,
  Hotstring level-zero exclusion, equal-level filtering, buffer update and
  matched callback are traceable. Oracle:
  [run_input_consumer_pipeline_oracle.sh](../tests/oracle/run_input_consumer_pipeline_oracle.sh)
  validates active/mirror/legacy X11 callbacks and level matrices, selected
  grab handoff/EndKey, broker physical InputHook callbacks with exactly two
  acceptances, and broker physical Hotstring with six acceptances/one trigger.
  IME composition/focus transactions remain M8; dynamic broker HotIf
  suppression remains a later M5b slice.
- **M5b-3 (custom combo + remap transaction pipeline)** — custom prefix/suffix
  state, wildcard-like modifier matching, prefix passthrough/native action,
  suffix key-up ownership and balanced suppression now live in the common
  normalized pipeline; EVDEV legacy state remains only as mirror/rollback and
  every event compares handled/suppress decisions. Standalone prefix callbacks
  and combo callbacks dispatch through the common InputLevel/ThisHotkey path.
  Broker ARB_DECISION sidebands are recorded in the same trace; a remap child
  EVENT links replacement transaction to its physical parent. Oracles:
  [run_input_combo_pipeline_oracle.sh](../tests/oracle/run_input_combo_pipeline_oracle.sh)
  validates the full A&B/A&C-up/~A&D/scan-code E&F/standalone-E corpus in
  active/mirror/legacy with zero decision mismatch; and
  [run_input_remap_pipeline_oracle.sh](../tests/oracle/run_input_remap_pipeline_oracle.sh)
  proves physical A→authoritative B child transaction, SendLevel 7 > InputLevel
  5 trigger, parent/child trace linkage, original suppression and a target
  sequence containing exactly B-down/B-up.
- **M5b-4 (dynamic HotIf deadline/fail-open)** — protocol v2 now carries
  DECISION_REQUEST/REPLY keyed by authority event, physical transaction,
  registration and registration-acceptance sequence. Callback HotIf rules are
  subscribed observe-only and registered as dynamic SUPPRESS arbitration;
  ahk_core evaluates the criterion on the main input thread, caches the
  normalized match and replies pass/suppress. The broker waits at most 60 ms,
  defaults keyboard timeouts and owner disconnects to replay, preserves sticky
  key-up after a suppress reply, and downgrades a rule observe-only after three
  consecutive timeouts; leases refresh every 60 s. This also closes the older
  Linux defect where callback HotIf used a permanent-zero SendMessageTimeout
  stub and FindVariant never evaluated criterion variants. Oracle:
  [run_input_dynamic_decision_oracle.sh](../tests/oracle/run_input_dynamic_decision_oracle.sh)
  independently covers pass, suppress, timeout, owner crash, slow-client
  downgrade and a false dynamic candidate falling through to the next static
  winner under one 60 ms event budget. It then proves a real ahk_core F7-down
  false→pass / F8-up true→sticky balanced suppression transition with one
  release callback and only the false pair target-visible, plus callback HotIf
  parity across X11 active/mirror/legacy. M5's normalized decision migration is
  now closed; IME composition/focus remains M8 host integration.
- **M6a (consented libei sender + EIS runtime capabilities)** — optional
  libei/liboeffis build support adds a user-session RemoteDesktop injection
  route without involving root inputd in portal consent. liboeffis owns the
  required CreateSession→SelectDevices→Start→ConnectToEIS ordering; the client
  binds seat-granted KEYBOARD/POINTER/BUTTON/SCROLL and compile+runtime-gated
  TEXT, loads the EIS XKB keymap, starts monotonic emulation sequences, frames
  every submission and uses ping/pong only as EIS-processed evidence. It never
  calls portal Notify* after obtaining EIS, never claims target delivery, and
  required mode forbids virtual-keyboard/uinput fallback. Device replacement
  reloads keymap/generation; a portal-owned EIS disconnect enters
  REAUTH_REQUIRED. Independent
  [run_input_libei_oracle.sh](../tests/oracle/run_input_libei_oracle.sh) uses a
  separate libeis receiver with pointer-first/delayed-keyboard devices to
  prove capability-specific waits, per-device keymaps/sequences, real EI
  keyboard/pointer/button/scroll and distinct down/up frames. It also covers
  explicit unrepresentable-Unicode/modifier failure with neutral release,
  top-level Click/XWayland-required routing, complete and mid-PressDuration
  device replacement, partial-grant compound-drag rejection, EIS-processed
  ping when libei ≥1.4, disconnect/required-route failure, and the no-libei
  build. A same-version feature-off pack template keeps `--pack` output a
  dependency-free single ELF even when the interactive runtime links libei.
  This direct-EIS oracle deliberately does not impersonate a portal;
  M6b remains open for GNOME/KDE native CreateSession/consent/ConnectToEIS,
  libei 1.6 TEXT, pause/revoke and persistent restore-token recovery evidence.
- `M7` unified D-Bus/portal/service recovery slice 1 (check_detail0901 §10.1–§10.5):
  the Linux COM layer (`core_com_dbus_linux.cpp`) replaces libdbus's pseudo-blocking
  `send_with_reply_and_block` with bounded `LinuxComPendingReply` calls. Deadlines
  are capped (default 5 s, `AHK_COM_CALL_TIMEOUT_MS` bounded 100–30000 ms), short
  dispatch slices pump the AHK message loop so script timers/hotkeys stay live
  during stalls, and timeouts cancel the pending call with machine-readable
  errors. Peer death surfaces as clean errors instead of freezing the main
  thread. Verified by static source guard
  [check_com_pending_calls.sh](../tests/oracle/check_com_pending_calls.sh) and
  fault-injecting [run_m7_dbus_oracle.sh](../tests/oracle/run_m7_dbus_oracle.sh).
- `M7` Wayland compositor restart recovery (check_detail0901 §9.2/§9.4): the
  Wayland session layer gained full generation-tracked teardown/rebuild — on
  fatal dispatch/flush errors the whole proxy tree (windows, virtual devices,
  managers, globals, registry, data-device clipboard) is destroyed in
  dependency order, and the next lazy connect rebuilds on the compositor's
  fresh socket (probing the runtime dir when the socket name changed) with an
  incremented `wayland_generation`. Reconnect is throttled (250 ms) and
  transient failures no longer stick after a first successful generation. One
  long-lived process is proven to survive a real SIGKILL of a headless sway,
  reconnect as generation 2 and deliver input through both generations:
  [run_m7_wayland_restart_oracle.sh](../tests/oracle/run_m7_wayland_restart_oracle.sh).
  `HotkeyBackendGet()` exposes `wayland_generation`/`wayland_active` and
  `--diag` prints the generation. The inputd/evdev restart domain of M7 is
  already gated by the M2 health oracle's broker-kill/reconnect scenario
  (same-PID reconnect, generation/authority bump, replay/suppress recovery)
  and the P0-1 replay fail-open oracle.
- `M7` §11.3 deterministic fault matrix — each recoverable fault class is
  gated by its own dedicated oracle that injects the fault for real and then
  asserts the recovery invariants (identical workload counters, no stuck
  keys, no duplicate registrations, visible state transitions), rather than
  by one combined soak: D-Bus service rotation/peer death →
  [run_m7_dbus_oracle.sh](../tests/oracle/run_m7_dbus_oracle.sh); real
  Wayland compositor kill+rebuild →
  [run_m7_wayland_restart_oracle.sh](../tests/oracle/run_m7_wayland_restart_oracle.sh);
  inputd broker kill/restart → the M2 health oracle (E scenario) plus the
  P0-1 replay fail-open fault oracle; clipboard owner churn → the
  doccheck clipboard slow-owner suites; GlobalShortcuts portal owner
  restart → run_portal_restart_oracle.sh; compositor STOP/CONT →
  run_compositor_stop_oracle.sh. All run in CI on every submission. A
  combined long-running soak additionally exists for RSS/fd slope and event
  consistency (run_mixed_soak.sh, nightly/weekly profiles); its fault
  schedule remains an optional manual extension, not a gate.
- `P2-10` Unicode/IME hardening (check_detail0901 §22), product-side fixes
  landed: (a) the Linux `MultiByteToWideChar` shim now enforces
  `MB_ERR_INVALID_CHARS` — malformed UTF-8 (overlong forms, surrogate
  halves, out-of-range code points, truncated tails) is rejected instead of
  silently recoded; (b) an invalid-UTF-8 IME commit is dropped BEFORE any
  state update — it never reaches LastCommit, the commit counter or the
  event trace; (c) IME shared state is mutex-protected (dispatch thread
  writes, hotstring/InputHook callback threads read concurrently); (d) the
  blocking framework probe (NameHasOwner round trips) moved off script
  threads into a throttled dispatch-context cache.  A corpus-driven
  D-Bus protocol oracle (combining marks, Devanagari/Arabic, skin tone,
  variation selector, family ZWJ as one hotstring unit, flag regional
  indicators, multi-char commits, supplementary plane, invalid UTF-8) is
  in progress: the event-stream/hotstring-unit assertions pass against a
  real Fcitx5-protocol producer, and the remaining work is isolating a
  same-session teardown in which a continuous UpdateFormattedPreedit signal
  stream from the producer correlates with the runtime exiting silently
  (no producer: the runtime's Report timer and clean exit work as
  designed).  The suspicion is the preedit freeze path in the hotstring
  character feed; the investigation is self-contained on the test-harness
  side and does not block any other milestone.
- `P2-10` corpus oracle gated: [run_p2_10_unicode_oracle.sh](../tests/oracle/
  run_p2_10_unicode_oracle.sh) drives the full §22 corpus over real
  Fcitx5 D-Bus signals and asserts (a) each commit arrives as a whole text
  transaction, (b) the family-ZWJ grapheme fires a hotstring exactly once
  as a single matching unit, (c) the event stream carries every code point
  (BMP values plus supplementary-plane surrogate halves), and (d) the
  invalid-UTF-8 entry is rejected at the D-Bus marshal layer.  Known
  follow-up bug: calling ImeStatus() from a hotstring callback thread still
  crashes (the engine snapshot fix removed one dangling pointer, but a
  second same-thread crash remains); scripts must read ImeStatus from the
  main thread or a timer until that is fixed.
- `P2-6` rich `ClipboardAll` (check_detail0901 §18) work starts here: the
  Linux port currently persists only the text representation (the Windows
  multi-format HGLOBAL walk in var.cpp runs unchanged against a text-only
  X11/Wayland clipboard), so ClipboardAll round-trips survive text but drop
  HTML/PNG/URI-list/custom-MIME representations.  The plan follows the
  audit's snapshot model: serialize every negotiated MIME with version,
  length, per-item checksum and bounded sizes, restore by re-offering all
  items, and gate with a multi-representation oracle (text+HTML, PNG,
  URI list, unknown MIME, bounded-failure).

For current user-facing status, use the repository [README](../README.md),
[Linux capability matrix](../docs-v2/docs/linux-port.htm),
[doc-check report](../tests/doccheck/CHECK_REPORT.md) and the latest GitHub
Release.
