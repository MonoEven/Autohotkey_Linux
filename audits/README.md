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
- **P0-3 (protocol v2 — M3 slice: identity + authorization + negotiation)** —
  v2 wire format on the same socket, discriminated by the `AHK2` magic (a v1
  u32 length can never equal it); HELLO/HELLO_ACK negotiation with protocol
  range, capability grants (OBSERVE always; SUPPRESS owner/root only;
  EXCLUSIVE/INJECT reserved for M4), and a per-process script nonce bound to
  a broker-assigned client_id; strictly monotonic client_seq; every broker-
  lane event carries an authoritative envelope (authority + generation +
  event_seq, source/confidence, send_level); machine-readable ERROR codes;
  v1 clients keep legacy semantics on the same socket; the client negotiates
  v2 first and falls back to v1 transparently. Oracles:
  [run_inputd_v2_protocol_oracle.sh](../tests/oracle/run_inputd_v2_protocol_oracle.sh)
  (25/25) and the extended
  [client framing oracle](../tests/oracle/run_inputd_client_framing_oracle.sh).
  Broker-owned injection/arbitration (M4) is the next step.

For current user-facing status, use the repository [README](../README.md),
[Linux capability matrix](../docs-v2/docs/linux-port.htm),
[doc-check report](../tests/doccheck/CHECK_REPORT.md) and the latest GitHub
Release.
