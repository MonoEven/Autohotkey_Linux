# AutoHotkey (Linux Port) #

**AutoHotkey v2.0.26 Linux port** — a free, open-source macro-creation and
automation utility for Linux. It is driven by a custom scripting language
with special provision for defining keyboard shortcuts, otherwise known as
hotkeys.

- **Language: AutoHotkey v2 only.** This port implements the v2 language.
  The v1 language, v1 commands and v1-to-v2 migration material are **not**
  part of this project.
- **Upstream**: https://www.autohotkey.com/ (the original Windows project;
  this repository is a fork of the v2.0.26 release with a Linux port of the
  interpreter on the `linux-port` branch).

## Features ##

- **Full X11 backend** (preferred when `DISPLAY` is set, including XWayland
  sessions): window management (`Win*`), controls (`Control*`), hotkeys
  (`XGrabKey`), pixel/monitor access (`PixelGetColor`, `PixelSearch`,
  `Monitor*`, `ImageSearch`), dialogs (`MsgBox`, `InputBox`,
  `FileSelect`/`DirSelect`), `ToolTip`, window shapes (`WinSetRegion`) and
  the whole doc-checked v2 API surface — **795/795** assertions pass under
  Xvfb.
- **Native Wayland backend** (used when no X display is available):
  xdg-shell windows, virtual keyboard/pointer input
  (`zwp_virtual_keyboard_v1` / `zwlr_virtual_pointer_manager_v1`),
  modifier-combo hotkey-style bindings and screen capture via
  `wlr-screencopy` — **13** Wayland + **229** XWayland assertions pass
  under sway.
- **327/327** built-in functions implemented (0 not implemented); see
  `tests/doccheck/CHECK_REPORT.md` for the per-module report.

## Install ##

Prebuilt packages are attached to each
[GitHub Release](https://github.com/MonoEven/Autohotkey_Linux/releases):

```bash
# Debian/Ubuntu
sudo apt install ./autohotkey-linux-<version>-amd64.deb

# Generic tarball (run the GUI or CLI installer from the unpacked tree)
tar xzf autohotkey-linux-<version>-amd64.tar.gz
cd autohotkey-linux
./tools/linux/install-gui.sh        # graphical wizard (zenity/yad)
# or
./tools/linux/install.sh --prefix ~/.local
```

The installer places the `ahk` launcher, the interpreter and the
documentation under the chosen prefix:

```bash
ahk --version        # AutoHotkey v2.0.26 Linux port (X11/Wayland)
ahk your-script.ahk
```

## Build from source ##

Requirements: CMake, a C++ compiler, X11 development headers
(`libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxtst-dev`),
Wayland development headers (`libwayland-dev wayland-protocols`) and
`libxkbcommon-dev`.

```bash
git clone --branch linux-port https://github.com/MonoEven/Autohotkey_Linux.git
cd Autohotkey_Linux
cmake -S . -B build-core
cmake --build build-core -j$(nproc)
build-core/source/linux/core/ahk_core your-script.ahk
```

Build a release package (tar.gz + .deb):

```bash
bash tools/linux/pack.sh
```

## Documentation ##

The official AutoHotkey v2 documentation pages (adapted for this port) are
published on GitHub Pages:

**https://monoeven.github.io/Autohotkey_Linux/**

The docs live in `docs-v2/`. The v1-to-v2 change documentation is omitted
because v1 is not supported by this port; see
[docs-v2/docs/linux-port.htm](docs-v2/docs/linux-port.htm) for the Linux
port overview (backends, differences from Windows, build/install notes).

## Differences from Windows AutoHotkey ##

- No GUI windows (`Gui`/`GuiControl`/`Menu` objects are Windows-only and
  are not implemented); use text-based tools or external GUIs.
- No COM, no Windows registry, no `DllCall` of Windows DLLs, no Win32
  messages to other windows.
- `Sound*`, tray icon and compiled-script (`.exe`) packaging are not
  available.
- Hotkeys work through `XGrabKey` on X11/XWayland; in pure Wayland they
  are unavailable (no global-hotkey protocol) — use XWayland for those.

## Support ##

- **Issues**: https://github.com/MonoEven/Autohotkey_Linux/issues
- The [AutoHotkey Community forum](https://www.autohotkey.com/boards/) is
  the primary upstream support channel for AutoHotkey v2.

## License ##

GNU General Public License — see [LICENSE](LICENSE).
