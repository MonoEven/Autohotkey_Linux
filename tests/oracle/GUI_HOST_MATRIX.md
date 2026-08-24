# Real GUI host capture matrix

These are real independent applications on the GNOME Wayland VM, not in-process compatibility objects. The source of truth is `run_gui_host_matrix.sh` and the JSON summaries in `tests/oracle/out/`.

| Host | Tested version | X11 window capture | Wayland AT-SPI controls | Result / limit |
|---|---:|---|---|---|
| GTK3 probe | 3.24.50 | Covered by X11 doc-check helpers | UTF-8 text read/write, Action, WinTitle application scope | Full for tested Entry/Button semantics |
| Qt6 probe | 6.9.2 | `WinGetList` finds the native Qt window | Entry read, Unicode append, Button Action changes text + external marker | Full for tested Entry/Button semantics |
| VS Code / Electron | 1.134.0 | EWMH/X11 finds the editor window | Window and Document node are visible with Text/Hypertext/Document interfaces | **Window-only**: Monaco source content is not exposed; Document.Text is U+FFFC |

VS Code was launched with both:

- `--force-renderer-accessibility --ozone-platform=wayland`
- `editor.accessibilitySupport = "on"`

Even then, the tested Electron destination exposed only the application/window/titlebar nodes. The unique source string was absent from the AT-SPI cache, so this repository deliberately does not claim VS Code editor-content automation. If a future VS Code/Electron version exposes Monaco lines, the versioned limitation oracle fails and the matrix must be upgraded instead of silently changing behavior.

Run the matrix:

```bash
bash tests/oracle/run_gui_host_matrix.sh \
  build-core/source/linux/core/ahk_core
```

Expected evidence includes exact GTK/Qt/VS Code versions, X11 window counts, AT-SPI interface metadata and per-host capability levels.
