# Changelog

## 0.2.0

- Native inline Debug Adapter Protocol implementation over the Linux DBGp core.
- Launch configurations and F5 debugging for `.ahk`/`.ah2` files.
- Verified filename breakpoints, continue, step into/over/out, stack frames,
  Local/Global scopes, scalar variables, evaluate and termination.
- Paged Array/Map/Object variable trees with nested handles; a two-page 20-item
  Array, Map string keys and nested Object are verified end-to-end.
- Exception breakpoint filter with caught-exception stop/evaluate/continue;
  `Error("D3-boom")` is inspected at line 6 and then handled normally.
- Persistent idle-script Pause with a bounded main-loop socket pump, explicit
  no-active-frame label and readable Global scope.
- Side-effect-free Linux D-Bus ComObject projection for adapter metadata and
  typed scalar values.
- Same-PID detach/reconnect commands using a signal-safe SIGUSR2 trigger;
  persistent-idle and tight-running scripts reattach, and IDE crashes leave the
  debuggee alive for a later reconnect.
- Real VS Code extension-host oracle observes breakpoint line 3, step line 4,
  `x=10`, `y=15`, expanded container values and clean termination.

## 0.1.0

- Initial VSC-1 extension.
- AutoHotkey v2 language configuration and syntax grammar.
- Native Linux runtime execution for files and selections.
- Output, diagnostics, task provider and stop command.
- Input-backend status bar and runtime capability tree from `--diag`.
- Honest VSC-2 boundary for interactive debugging.
