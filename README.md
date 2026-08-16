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
  the whole doc-checked v2 API surface — **842/842** assertions pass under
  Xvfb (29 of them exercise `DllCall`).
- **Native Wayland backend** (used when no X display is available):
  xdg-shell windows, virtual keyboard/pointer input
  (`zwp_virtual_keyboard_v1` / `zwlr_virtual_pointer_manager_v1`),
  modifier-combo hotkey-style bindings and screen capture via
  `wlr-screencopy` — **13** Wayland + **229** XWayland assertions pass
  under sway.
- **System clipboard**: `A_Clipboard` integrates with the desktop
  clipboard (X11 CLIPBOARD selection on X11/XWayland, wl_data_device on
  Wayland; process-internal fallback headless), verified cross-process
  with xclip.
- **DllCall for native libraries**: calls functions in Linux shared
  objects (`.so`) via dlopen/dlsym + libffi — full type support
  (`Int`/`Int64`/`Short`/`Char`/`Float`/`Double`/`Ptr`/`Str`/`AStr`/
  `WStr`), by-address `&Var` output parameters and HRESULT-style error
  reporting (29 doc-check assertions).
- **COM over D-Bus**: `ComObject("service")` proxies a D-Bus bus service;
  method calls and property access map to D-Bus; `ComValue` wraps typed
  values (18 doc-check assertions).  Windows COM interfaces do not exist
  on Linux.
- **327/327** built-in functions implemented (0 not implemented); see
  `tests/doccheck/CHECK_REPORT.md` for the per-module report.
- **CI**: GitHub Actions builds both the regular and ASan binaries and
  runs the full suite (headless, Xvfb, Wayland, XWayland) on every push.

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
Wayland development headers (`libwayland-dev wayland-protocols`),
`libxkbcommon-dev`, `libffi-dev` (DllCall) and `libdbus-1-dev` (COM/D-Bus).

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
- COM is implemented over **D-Bus** (no IUnknown/IDispatch/SafeArray
  pointers, no COM events); `ComObjQuery`/`ComObjConnect`/`ComObjArray`
  raise an error.
- No Windows registry, no Win32 messages to other windows
  (`SendMessage`/`PostMessage`/`OnMessage` are not available).
- `DllCall` loads native **.so** shared objects — Windows DLLs are not
  loadable.
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
