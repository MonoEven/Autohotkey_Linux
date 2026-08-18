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
- **Latest build**: `v2.0.26-linux.10` (see
  [Releases](https://github.com/MonoEven/Autohotkey_Linux/releases)).
  Doc-check **1053/1053** (regular + ASan), regression 27/27,
  Wayland 13/13, XWayland 247/247.

## Features ##

- **Full X11 backend** (preferred when `DISPLAY` is set, including XWayland
  sessions): window management (`Win*`), controls (`Control*`), hotkeys
  (`XGrabKey` — keyboard + mouse buttons, `~`/Off/HotIf pass-through,
  BadAccess conflict reporting, left/right modifiers, wildcard), **hotstring
  expansion** (typed-text capture engine, all core options), **InputHook
  live key capture**, pixel/monitor access (`PixelGetColor`, `PixelSearch`,
  `Monitor*`, `ImageSearch`), dialogs (`MsgBox`, `InputBox`,
  `FileSelect`/`DirSelect`), `ToolTip`, window shapes (`WinSetRegion`),
  GTK3 `Gui`/`Menu` and the whole doc-checked v2 API surface —
  **1053/1053** assertions pass under Xvfb.
- **Native Wayland backend** (used when no X display is available):
  xdg-shell windows, virtual keyboard/pointer input
  (`zwp_virtual_keyboard_v1` / `zwlr_virtual_pointer_manager_v1`),
  modifier-combo hotkey-style bindings and screen capture via
  `wlr-screencopy` — **13** Wayland + **247** XWayland assertions pass
  under sway.
- **Hotstring** (round-32): real expansion of `Hotstring()` from the typed
  stream — the all-keys capture engine holds trigger prefixes and, on a
  full match (end char or `*`), suppresses the trigger and sends the
  replacement (or runs the `X`-option callback).  Options `C`/`*`/`O`/`X`,
  case conforming, `HotIf` criteria; verified against the independent
  `xkeycap` client.
- **InputHook** (round-33): live key capture while a hook is `InProgress` —
  buffer fill, single-char/named end keys (`EndChar`/`EndKey`), match list,
  backspace undo, input suppression; `OnChar`/`OnKeyDown` notifications
  still pending the unified event-stream work.
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
- **367/370** built-in functions implemented (3 are intentionally
  not-implemented with a clear error: `ComObjArray`, `TraySetIcon` and
  `TrayTip`); see `tests/doccheck/CHECK_REPORT.md` for the per-module
  report and `tests/doccheck/worklist.tsv` for the per-function status.
- **CI**: GitHub Actions builds both the regular and ASan binaries and
  runs the full suite (headless, Xvfb, Wayland, XWayland) on every push;
  dependency install is hardened against runner apt-mirror stalls
  (IPv4/timeouts/retries + azure.archive.ubuntu.com fallback).

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
ahk uninstall        # cleanly remove this installation
ahk update           # update to the latest GitHub release
ahk update 2.0.26-linux.8   # or a specific release (upgrade or downgrade)
```

## Build from source ##

Requirements: CMake, a C++ compiler, X11 development headers
(`libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxtst-dev libxi-dev`),
Wayland development headers (`libwayland-dev wayland-protocols`),
`libxkbcommon-dev`, `libffi-dev` (DllCall), `libdbus-1-dev` (COM/D-Bus) and
`libgtk-3-dev` (GUI).

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
The language switcher on the docs site offers English and Chinese only
(a maintained Chinese overview is at `docs-v2/docs/zh.htm`); it never
leaves this fork.

## Differences from Windows AutoHotkey ##

- **GUI**: `Gui`/`GuiControl`/`Menu` are implemented over **GTK3** and work
  on X11/XWayland (they need a display; pure headless sessions raise a
  clear error).
- COM is implemented over **D-Bus** (no IUnknown/IDispatch/SafeArray
  pointers, no COM events); `ComObjArray` raises an error.
- No Windows registry, no Win32 messages to other windows
  (`SendMessage`/`PostMessage`/`OnMessage` are not available).
- `DllCall` loads native **.so** shared objects — Windows DLLs are not
  loadable.
- **Sound**: `SoundBeep` / `SoundPlay` are implemented (`aplay`/`paplay`);
  `SoundGet*`/`SoundSet*` need `pactl`/`amixer` installed.  There is no
  tray icon, so `TrayTip` / `TraySetIcon` raise a clear error.
- Hotkeys, hotstrings and InputHook capture work through **XGrabKey** +
  the typed-text capture engine on X11/XWayland; in pure Wayland there is
  no global-hotkey input protocol, so those need an XWayland session.

## Support ##

- **Issues**: https://github.com/MonoEven/Autohotkey_Linux/issues
- The [AutoHotkey Community forum](https://www.autohotkey.com/boards/) is
  the primary upstream support channel for AutoHotkey v2.

## License ##

GNU General Public License — see [LICENSE](LICENSE).
