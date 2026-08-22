# AutoHotkey (Linux Port) #

**AutoHotkey v2.0.26 Linux port** — a free, open-source macro-creation and
automation utility for Linux. It is driven by a custom scripting language
with special provision for defining keyboard shortcuts, otherwise known as
hotkeys.

> **Status: technology preview (not "official release").** The v2 language
> coverage is broad (367 built-ins, 1120 doc-check assertions) and the
> X11/XWayland backend is usable for real automation; native-Wayland
> global hotkeys (GNOME Shell extension / Portal / evdev lanes), Unicode
> input on compositors without a virtual keyboard, AT-SPI control
> automation and IME state detection are implemented and machine-verified,
> while full cross-app automation depth and large-scale community
> validation are still young. See
> [docs-v2/docs/linux-port.htm](docs-v2/docs/linux-port.htm) for the honest
> capability/limitation matrix before relying on it in production.

- **Language: AutoHotkey v2 only.** This port implements the v2 language.
  The v1 language, v1 commands and v1-to-v2 migration material are **not**
  part of this project.
- **Upstream**: https://www.autohotkey.com/ (the original Windows project;
  this repository is a fork of the v2.0.26 release with a Linux port of the
  interpreter on the `linux-port` branch).
- **Latest build**: `v2.0.26-linux.15` (see
  [Releases](https://github.com/MonoEven/Autohotkey_Linux/releases)).
  Doc-check **1120/1120** (regular + ASan), regression 27/27,
  Wayland 17/17, XWayland 247/247.

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
  **1120/1120** assertions pass under Xvfb.
- **Unicode text input** (linux.13/linux.14): `SendText`/`Send` and hotstring
  replacements deliver non-ASCII characters (Chinese, Japanese, Korean,
  accented Latin, Emoji) on X11/XWayland via keysym transmission (a spare
  keycode is temporarily remapped when the layout has no binding, like
  xdotool; linux.14 serializes the borrow through an X selection lease so
  concurrent AHK processes never clash, with a dual-process doc-check
  regression (two independent processes send different CJK characters to
  the same X server simultaneously); on pure Wayland non-ASCII runs use
  the controlled clipboard-paste fallback (which waits for the target to
  consume the offer, restores an empty original to empty, and can be
  disabled with `AHK_WAYLAND_PASTE=0`) whenever a key-injection lane
  exists — the virtual-keyboard protocol (wlroots/sway) or the uinput
  virtual keyboard (`/dev/uinput`, GNOME/KWin; `AHK_UINPUT=0` disables);
  where no lane exists a clear error names the character instead of
  silently dropping it.
- **Native Wayland backend** (used when no X display is available):
  xdg-shell windows, virtual keyboard/pointer input
  (`zwp_virtual_keyboard_v1` / `zwlr_virtual_pointer_manager_v1`),
  screen capture via `wlr-screencopy` — **17** Wayland + **247**
  XWayland assertions pass under sway.
- **Unified input backend** (linux.12+): `AHK_INPUT_BACKEND=auto|x11|
  portal|gnome-shell|evdev` selects the global-hotkey backend — X11
  `XGrabKey`/`XGrabButton` on X sessions; the **GNOME Shell extension**
  (GNOME 49 Wayland, zero-confirmation exclusive hotkeys:
  `1::`/`^1::`/`F12::` grab the physical key with no per-binding dialog,
  with automatic re-registration after extension disable/enable (VM
  verified) and fail-open release on exit; linux.15 fixes
  `Hotkey()` never reaching the Wayland backends on Wayland sessions
  (the X-only guard is gone), and the Activated signal sender check now
  matches the broker's unique bus name (D-Bus delivers unique names, not
  well-known ones) — the extension backend is end-to-end usable; the
  standard **XDG Global Shortcuts Portal** backend remains the
  KDE/other-Wayland fallback (deny/cancel: no crash, no fire, clean
  exit — regression suite).
  `AHK_FORCE_GLOBAL_SHORTCUTS=1` keeps its documented meaning (explicitly
  require the Portal backend; only `1/true/yes/on` count as true, and an
  unknown `AHK_INPUT_BACKEND` value prints a clear startup warning).
- **Hotstring** (round-32): real expansion of `Hotstring()` from the typed
  stream — the all-keys capture engine holds trigger prefixes and, on a
  full match (end char or `*`), suppresses the trigger and sends the
  replacement (or runs the `X`-option callback).  Options `C`/`*`/`O`/`X`,
  case conforming, `HotIf` criteria; verified against the independent
  `xkeycap` client.  Round-34: Unicode trigger words (CJK, accented text)
  are matched character-for-character through the Unicode keysym stream.
- **InputHook** (round-33/34): live key capture while a hook is `InProgress` —
  buffer fill, single-char/named end keys (`EndChar`/`EndKey`), match list,
  backspace undo, input suppression; `OnChar`/`OnKeyDown`/`OnKeyUp`
  notifications (round-34) are queued by the capture engine and fired from
  the main-loop dispatch (Windows semantics: VK/SC for keys, the character
  for OnChar, Unicode included).
- **System clipboard**: `A_Clipboard` integrates with the desktop
  clipboard (X11 CLIPBOARD selection on X11/XWayland, wl_data_device on
  Wayland; process-internal fallback headless), verified cross-process
  with xclip.  The bounded read/wait window is configurable:
  `AHK_CLIPBOARD_TIMEOUT_MS` (default 2000) covers slow clipboard owners
  (a slow app that only serves the request late); the slow-owner
  regression is part of the doc-check suite.
- **AT-SPI control automation** (Wayland): `ControlGetText`,
  `ControlSetText` and `ControlClick` in a Wayland session (GNOME 49
  verified) resolve the control as an AT-SPI accessible name over the
  org.a11y.atspi bus (read text / `EditableText.SetTextContents` /
  `Action.DoAction("click")`), so GTK/Qt/Electron apps with
  accessibility enabled can be driven without an X display.  Verified
  end-to-end on a GTK3 app (read label → click button → label changed).
- **IME active-state detection**: `ImeGetState()` returns the active
  input-method framework (ibus / fcitx5 by session-bus owner) and the
  current XKB group (the effective layout/IME group on X11).  Preedit
  reading, IME toggling and wlroots input-method-v2 text delivery are
  documented as out of scope (mutter does not implement the zwlr
  protocol — see docs/IME-Integration.md for the honest record).
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
# Debian/Ubuntu    (system-wide; extension ships in the deb, dpkg-managed)
sudo apt install ./autohotkey-linux-<version>-amd64.deb
#  ... after install: gnome-extensions enable ahk-global-hotkeys@autohotkey.org
#     (one line, once per user; then restart GNOME Shell / Alt+F2 'r')

# Fedora / openSUSE (RPM; system-wide, extension bundled, dnf-managed)
sudo dnf install ./autohotkey-linux-<version>-amd64.rpm

# Arch (PKGBUILD; system-wide, extension bundled, pacman-managed)
#   (build from tools/linux/PKGBUILD into your package repo / AUR)

# Generic tarball (run the GUI or CLI installer from the unpacked tree)
tar xzf autohotkey-linux-<version>-amd64.tar.gz
cd autohotkey-linux
./tools/linux/install-gui.sh        # graphical wizard (zenity/yad)
# or
./tools/linux/install.sh --prefix ~/.local --gnome-extension

# AppImage (portable single file; extension copied user-side on demand)
./autohotkey-linux-<version>-amd64.AppImage --install-extension
```

The installer places the `ahk` launcher, the interpreter and the
documentation under the chosen prefix:

```bash
ahk --version         # AutoHotkey v2.0.26 Linux port v2.0.26-linux.15
ahk --check           # integrity, install method, latest release check
ahk your-script.ahk
ahk --uninstall       # cleanly remove this installation
ahk --update          # update to the latest GitHub release
ahk --update 2.0.26-linux.15   # or a specific release (upgrade or downgrade)
```

> Note: launcher commands are `--` prefixed (`--update`, `--uninstall`,
> `--check`, `--version`) so plain names such as `ahk update.ahk` are always
> treated as scripts.  Every release since v2.0.26-linux.15 stays published
> on the GitHub Releases page (with a `CKSUMS.txt` of SHA-256 hashes), so
> the tarball's `ahk --update <VER>` can upgrade **or downgrade** to any
> published release; fixes and bespoke state can therefore be rolled back
> to an older known release.  Releases before linux.15 were removed when
> the new installer line shipped; their source remains available on the
> `v2.0.26-linux.*` git tags for source-level rollback.

> **What `ahk --update` / `ahk --uninstall` do by install method.** The
> launcher detects how the copy was installed and routes the command
> accordingly:
> - **tarball / user prefix**: `ahk --update [VER]` downloads the release
>   tarball and reinstalls over the same prefix (upgrade or downgrade);
>   `ahk --uninstall` removes the prefix files.  The GNOME extension is
>   never touched by either.
> - **`.deb` (Debian/Ubuntu)**: `--update`/`--uninstall` print the
>   `apt`/`dpkg` equivalent (`sudo apt install --only-upgrade
>   autohotkey-linux` / `sudo apt remove autohotkey-linux`) — package
>   state stays consistent.  The extension is dpkg-managed: package
>   upgrade updates it, `apt remove` deletes it.
> - **RPM (Fedora/openSUSE)**: `--update`/`--uninstall` print the
>   `dnf`/`rpm` equivalent (`sudo dnf update autohotkey-linux` /
>   `sudo dnf remove autohotkey-linux`).  The extension ships with the
>   package and is removed with it.
> - **Arch (pacman)**: `--update`/`--uninstall` print
>   `sudo pacman -Syu` / `sudo pacman -Rns autohotkey-linux`.  The
>   extension ships with the package and is removed with it.
> - **AppImage**: `--install-extension` copies the bundled extension to
>   the user's GNOME extensions dir and enables it; `--update` downloads
>   the newest AppImage next to the running file.  (The launcher-package
>   commands apply to the deb/RPM/pacman/tarball installs; the AppImage
>   handles these two passthrough commands itself and runs scripts
>   otherwise.)  See the release notes for the checksums.

> **First install on GNOME Wayland?** Run the installer once with
> `--gnome-extension` (e.g. `./tools/linux/install.sh --prefix ~/.local
> --gnome-extension`), or answer "y" when the interactive installer asks,
> to also copy and enable the optional global-hotkeys GNOME Shell
> extension in one step.  **You must then restart GNOME Shell** (log
> out/in, or press Alt+F2 and type `r`) — a freshly-placed extension only
> loads at the next Shell start, and `gnome-extensions enable` alone
> registers the choice without hot-loading a brand-new extension.  After
> the restart `ahk --check` shows its state.  Later core updates never
> touch the extension; the `.deb`/tarball itself still never activates it
> by default (no GNOME session = nothing happens).

> **What (un)install/update do and do not touch.** The installer
> (`install.sh`), `ahk --uninstall` and `ahk --update` only manage the files
> under the chosen install prefix (the `ahk` launcher, the interpreter
> `ahk_core`, the include files and the bundled documentation).  They do
> **not** install, activate, deactivate, overwrite or delete the **GNOME
> Shell extension** (`ahk-global-hotkeys@autohotkey.org`) — that is a
> separate, user-scoped component living in
> `~/.local/share/gnome-shell/extensions/` (or `/usr/share/gnome-shell/
> extensions/`), enabled on the GNOME Shell side with
> `gnome-extensions enable`.  In particular **`ahk --update` never touches
> the extension**, so an installed/enabled extension survives every core
> update (the extension is itself version-loose with a stable D-Bus
> surface; see `docs-v2/docs/howto/Install.htm`).  `ahk --uninstall` also
> leaves the extension in place — remove it separately with
> `gnome-extensions disable ...` and by deleting its directory.  The
> `ahk --check` command reports whether the extension is installed and
> enabled.

> **Do not confuse the layers when updating the core**: (re)installing a
> newer interpreter, or rolling back to an older one, never registers,
> deregisters or re-registers the GNOME extension.  If a future release
> ships a newer extension, install/update it as an extension update
> (replace the directory at the same location), not as part of the core
> package; the same guarantee applies in the reverse direction (rolling
> back the core never un-installs the extension).

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
The docs site is English-only.

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
  the typed-text capture engine on X11/XWayland; in Wayland sessions
  global hotkeys go through the GNOME Shell extension or the Global
  Shortcuts Portal (see the input-backend bullet above; bare `~`-style
  passthrough and full remapping on Wayland are the evdev/ahk-inputd
  roadmap — see `docs-v2/docs/linux-port.htm` for the compatibility
  matrix and the evdev/inputd design notes).

## Contributors ##

- [**MonoEven**](https://github.com/MonoEven) — author and maintainer of
  the Linux port: source-level port of the interpreter, X11/Wayland/evdev
  input backends, the GNOME Shell extension, AT-SPI and IME integration,
  packaging/CI and the documentation in this repository.

## Related work and credits ##

This port is an independent implementation, but it builds on the ideas and
feedback of the wider AutoHotkey-on-Linux community:

- [**AHK_X11**](https://github.com/phil294/AHK_X11) — a from-scratch
  reimplementation of AutoHotkey v1 (2004-era syntax) in
  [Crystal](https://crystal-lang.org/), X11-only (XRecord global key
  monitoring + XTEST injection), with its own single-binary AppImage
  distribution.  It is the most widely used AutoHotkey-on-Linux project
  before this one and a frequent reference point for X11 key capture and
  injection behavior (see also the [AHK_X11 thread on the AutoHotkey
  forum](https://www.autohotkey.com/boards/viewtopic.php?f=81&t=106640)).
- [AutoHotkey v2](https://www.autohotkey.com/) — the upstream Windows
  project this fork is based on; all language semantics come from it.
- The [AutoHotkey Community forum](https://www.autohotkey.com/boards/) —
  the primary support channel for the language.

## Support ##

- **Issues**: https://github.com/MonoEven/Autohotkey_Linux/issues
- The [AutoHotkey Community forum](https://www.autohotkey.com/boards/) is
  the primary upstream support channel for AutoHotkey v2.

## License ##

GNU General Public License — see [LICENSE](LICENSE).
