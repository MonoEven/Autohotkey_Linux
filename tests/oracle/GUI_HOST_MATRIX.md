# Real GUI host capture matrix

These are real independent applications on the GNOME Wayland VM, not in-process compatibility objects. The source of truth is `run_gui_host_matrix.sh` and the JSON summaries in `tests/oracle/out/`.

| Host | Tested version | X11 window capture | Wayland AT-SPI controls | Result / limit |
|---|---:|---|---|---|
| GTK3 probe | 3.24.50 | Covered by X11 doc-check helpers | UTF-8 text read/write, Action, WinTitle application scope | Full for tested Entry/Button semantics |
| Qt6 probe | 6.9.2 | `WinGetList` finds the native Qt window | Entry read/Unicode append, Button Action, QListWidget Selection, QSlider Value 25&rarr;64; pending-call Timer responsiveness and errno bridge | Full for tested Entry/Button/List/Slider semantics |
| OpenJDK Swing + Java ATK Wrapper | JDK 21.0.11 / wrapper 0.42.1 | Real XWayland JFrame | JTextField read/write, JButton Action via `NActions` property, JList Selection, JSlider Value read; WinTitle cache is one app/20 nodes | Value write is **explicit EIO**: Java bridge advertises readwrite and returns success but ignores the set; runtime readback rejects fake success |
| LibreOffice Calc | 25.8.7.3 | Real XWayland Calc/CSV import | Import-dialog Action, main-window WinTitle scope, Sheet Table/Selection metadata; one app/1272 nodes within 2s | Table metadata is real (1,048,576&times;16,384); virtual cell content is not in Cache and no Table-cell Control API is claimed |
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

Expected evidence includes exact GTK/Qt/Java/LibreOffice/VS Code versions, X11 window counts, AT-SPI interface metadata and per-host capability levels. Java and LibreOffice are optional at aggregation time, but when their packages are installed their child oracles must pass; absence is reported as `not-installed`, never as support.
