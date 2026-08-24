# VSC-2 Linux interactive debugger plan

VSC-1 intentionally provides run/stop/tasks/diagnostics, not fake breakpoints or stepping. The upstream AutoHotkey source contains a mature DBGp engine, but the Linux target deliberately excludes it today.

## Evidence from the Linux compile probe

A local/VM compile probe enabled `CONFIG_DEBUGGER`, added `Debugger.cpp` to the Linux target and supplied basic POSIX socket wrappers. It was reverted after collecting blockers so the main branch remains buildable.

What already works conceptually:

- `Debugger::PreExecLine()` checks breakpoints, step state and pending socket commands before every executable line.
- The command set includes init/status/run, step_into/over/out, line breakpoints, stack/context/property and stdout/stderr redirection.
- The engine is a DBGp **client**, matching the normal IDE-listens/runtime-connects topology.

Concrete Linux blockers found:

1. `source/config.h` disables `CONFIG_DEBUGGER` under `__linux__`.
2. `source/linux/core/CMakeLists.txt` does not compile/link `Debugger.cpp`.
3. `Debugger.h` unconditionally includes Winsock; basic socket calls are portable, but `WSAAsyncSelect` must map to `PreExecLine`/Linux-main-loop polling.
4. Enabling the macro also enables Windows debugger-rich-error UI code in `error.cpp` (`CHARFORMAT`, `EM_EXGETSEL`) which must be separated from protocol support.
5. `Debugger.cpp` includes Windows COM property marshalling (`script_com.h`, IDispatch/SAFEARRAY). Linux needs a debugger-property adapter for native objects/D-Bus values or an explicit unsupported type marker.
6. Windows-only string/number helpers (`CStringUTF8FromTChar`, `_itoa`, `_i64toa`, `_atoi64`, `_stricmp`) need narrow POSIX replacements.
7. Linux `main_linux.cpp` has no `/Debug`/`--debug` parser or post-load connect-and-break phase.
8. Persistent/idle scripts need debugger socket pumping in the Linux main loop in addition to `PreExecLine` polling.

## Dependency-ordered VSC-2 milestones

### D1 — protocol-minimal Linux DBGp core

- POSIX socket transport.
- `--debug host:port` and initial break.
- init/status/run/stop/detach.
- line breakpoints and step_into/over/out.
- stack/context/basic scalar property values.
- Explicit `unsupported` property type for COM-only values.

Acceptance: an independent Python IDE server validates framing, init packet, transaction IDs, breakpoint hit line, step line, context scalar and clean detach.

### D2 — VS Code debug adapter

- A small Debug Adapter Protocol process translating launch, breakpoints, threads, stackTrace, scopes, variables, continue and stepping to DBGp.
- No VS Code command is contributed as a debugger until D1 protocol acceptance passes.

Acceptance: a real VS Code extension-host test launches the adapter, hits two source breakpoints, steps, reads a variable and terminates.

### D3 — advanced values and persistence

- Arrays/Maps/objects with paging and depth limits.
- Exception breakpoints.
- Idle/persistent script break requests.
- Reconnect/detach and crash cleanup.

## Non-goals

- Reimplementing the Windows rich-edit debugger UI on Linux.
- Pretending process execution is interactive debugging.
- Claiming Windows COM property inspection on the D-Bus compatibility layer without a type adapter.
