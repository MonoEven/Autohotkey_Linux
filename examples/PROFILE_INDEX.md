# Example Environment Profiles

Examples are mapped to the least environment in which their Linux behavior is actually verified.

| Profile | Primary examples | Functions with evidence | Command |
|---|---:|---:|---|
| `dbus` | 7 | 9 | `bash tests/doccheck/run_check.sh --xvfb "$BIN"` |
| `desktop-session` | 2 | 8 | `bash tests/oracle/run_gui_host_matrix.sh "$BIN"` |
| `headless` | 124 | 153 | `bash tests/doccheck/run_check.sh "$BIN"` |
| `host-tools` | 9 | 15 | `bash tests/doccheck/run_check.sh --xvfb "$BIN"` |
| `interactive` | 1 | 1 | `"$BIN" examples/interactive/input_box.ahk` |
| `lifecycle` | 2 | 2 | `bash examples/run.sh lifecycle "$BIN"` |
| `safety-boundary` | 1 | 1 | `"$BIN" examples/safety/shutdown_requires_confirmation.ahk` |
| `wayland` | 0 | 18 | `bash tests/doccheck/wayland_run.sh "$BIN"` |
| `x11` | 224 | 233 | `bash tests/doccheck/run_check.sh --xvfb "$BIN"` |

`safety-boundary` examples refuse destructive behavior by default. `interactive` examples are intentionally not run unattended.
