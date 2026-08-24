# VSC-2 Linux interactive debugger

VSC-1 provides run/stop/tasks/diagnostics. VSC-2 adds real interactive debugging in dependency order: the runtime protocol must pass independently before VS Code contributes a debug adapter.

## D1 — Linux DBGp core: delivered

The upstream AutoHotkey DBGp engine is now compiled into `ahk_core` on Linux. The runtime accepts either:

```text
ahk_core --debug HOST:PORT script.ahk [args...]
ahk_core --debug=HOST:PORT script.ahk [args...]
```

It connects to the listening IDE as a DBGp client, sends the protocol 1.0 init packet and breaks before auto-execute, matching the Windows topology and lifecycle.

Port work completed:

- POSIX socket compatibility for connect/send/recv/FIONREAD/close.
- Correct 32-bit Linux `FIONREAD` result handling (LP64 `u_long` caused false pending reads and blocking recv).
- Standard Unix `file:///absolute/path` generation and decoding without converting `/` to `\\`.
- Linux UTF-8/CString and integer-format helpers.
- Correct exact-size `WideCharToMultiByte` buffers (the old shim incorrectly required four bytes even for one-byte ASCII).
- Windows attach-message and rich-edit call-stack UI isolated from the protocol core.
- Windows `script_com.h` dependency removed on Linux. D-Bus compatibility objects remain visible as objects but deliberately expose no fake IDispatch/SAFEARRAY child projection.
- `--debug` parsing, initial connect/break and ordinary script-argument preservation.
- Existing `PreExecLine` breakpoint/step/pending-command hooks and debugger call stack enabled.

Independent acceptance is `tests/oracle/run_dbgp_oracle.sh`. Its Python IDE server validates:

- NUL-delimited DBGp length framing and UTF-8 XML;
- init language/protocol/file URI;
- feature negotiation and transaction IDs;
- a filename-scoped breakpoint at line 3;
- run-to-break and stack line;
- Local/Global context names and full global `context_get`;
- scalar `property_get` (`x=10`);
- step-into to line 4 and `y=15`;
- detach response and resumed process result `value=30`.

The oracle is independent of the runtime and is a required core CI gate.

## D2 — VS Code debug adapter: delivered

The dependency-free inline Debug Adapter Protocol implementation translates:

- launch/terminate;
- setBreakpoints;
- threads/stackTrace/scopes/variables and scalar evaluate;
- continue, next, stepIn and stepOut;
- stopped/exited/terminated and stdout/stderr events.

Two layers enforce acceptance. `run_dap_adapter_oracle.sh` is an external DAP
client over the real runtime; it proves breakpoint line 3, step line 4,
`x=10`, `y=15` and result 30. `run_vscode_extension_oracle.sh` installs the
0.2.0 VSIX into VS Code 1.134, starts the contributed `ahk-linux` debugger,
and observes the same breakpoint/step/variables through a DebugAdapterTracker
before clean termination.

## D3 — advanced values and persistence

### Delivered: paged Array/Map/Object trees

- DBGp Array/Map `numchildren` now reports exact built-in enumerable size instead
  of counting only the lazy `__Enum` marker.
- DAP advertises variable paging and keeps per-stop object handles.
- `variables(start,count)` translates to `property_get -p PAGE`, including
  page-boundary slicing and nested object handles.
- Three independent layers prove a 20-item Array over two pages, Map string keys
  (`first=101`, `second=202`) and nested Object (`alpha=A`, `beta=42`): raw DBGp,
  external DAP client and real VS Code 1.134 variable-tree requests.

Remaining D3 work:

- an explicit debugger projection for Linux D-Bus compatibility objects;
- exception breakpoints;
- debugger break requests while a persistent script is idle in the Linux main loop;
- reconnect/detach and crash cleanup.

## Non-goals

- Reimplementing the Windows rich-edit debugger UI on Linux.
- Pretending process execution is interactive debugging.
- Claiming Windows COM property inspection on the D-Bus compatibility layer without a real type adapter.
