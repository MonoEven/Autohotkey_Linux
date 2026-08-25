# AutoHotkey v2 for Linux

<p align="center"><img src="docs-v2/docs/static/ahk_logo.svg" alt="AutoHotkey" width="420"></p>

A source-level Linux port of **AutoHotkey v2.0.26** for X11, XWayland and
native Wayland. It keeps the v2 language and adapts desktop integration to
Linux APIs: X11/XTEST, Wayland protocols, XDG portals, GNOME Shell, evdev /
uinput, GTK3, AT-SPI, D-Bus and StatusNotifierItem.

> **Status: technology preview.** The interpreter and X11/XWayland backend
> have broad machine-tested coverage, but this is an independent port rather
> than an official upstream release. Read the
> [Linux capability matrix](docs-v2/docs/linux-port.htm) before production use.

**Current release:** [`v2.0.26-linux.19`](https://github.com/MonoEven/Autohotkey_Linux/releases/latest)
· **Docs:** [GitHub Pages](https://monoeven.github.io/Autohotkey_Linux/)
· **Detailed results:** [CHECK_REPORT.md](tests/doccheck/CHECK_REPORT.md)
· **Changelog:** [ChangeLog.htm](docs-v2/docs/ChangeLog.htm)

## Install

Download the package for your system from
[GitHub Releases](https://github.com/MonoEven/Autohotkey_Linux/releases):

```bash
# Debian / Ubuntu
sudo apt install ./autohotkey-linux-2.0.26-linux.19-amd64.deb

# Fedora / openSUSE
sudo dnf install ./autohotkey-linux-2.0.26-linux.19-x86_64.rpm

# AppImage
chmod +x autohotkey-linux-2.0.26-linux.19-x86_64.AppImage
./autohotkey-linux-2.0.26-linux.19-x86_64.AppImage script.ahk

# Generic tarball
./tools/linux/install.sh --prefix ~/.local --yes
```

Arch users can build [`tools/linux/PKGBUILD`](tools/linux/PKGBUILD). A Flatpak
manifest is provided at
[`tools/linux/org.autohotkey.AHK.yml`](tools/linux/org.autohotkey.AHK.yml).
For GNOME Wayland global hotkeys, install or enable the optional bundled GNOME
Shell extension; see the [installation guide](docs-v2/docs/howto/Install.htm).

## Quick start

```ahk
#Requires AutoHotkey v2.0

::btw::by the way

F6::
{
    SendText("Hello from AutoHotkey on Linux!")
}

^!o::
{
    Run("xdg-open https://www.autohotkey.com/docs/v2/")
}
```

Run it with:

```bash
ahk script.ahk
```

Create a self-contained executable:

```bash
ahk_core --pack my-script script.ahk
./my-script
```

## Complete examples

[`examples/FUNCTION_COVERAGE.md`](examples/FUNCTION_COVERAGE.md) maps every one
of the **370 Linux IMPL functions** to a generated function page and an exact
Linux-verified source line; 230 also include upstream reference code. Environment
profiles separate headless, X11, Wayland, D-Bus, real desktop, interactive,
lifecycle and destructive-safety boundaries.

```bash
python3 tools/gen_examples_catalog.py --check
bash examples/run.sh all-curated build-core/source/linux/core/ahk_core
bash examples/run.sh all-verified build-core/source/linux/core/ahk_core
```

The unattended runner never opts into `Shutdown`; that example refuses by
default. Release tar/deb/RPM payloads include the complete examples catalog.

## What works

| Area | Linux implementation |
|---|---|
| Language/runtime | AutoHotkey v2 syntax, objects, functions, classes, timers, files and processes |
| X11/XWayland | `Win*`, `Control*`, hotkeys, hotstrings, InputHook, Unicode Send, pixels/monitors, dialogs and GTK3 GUI |
| Native Wayland | xdg-shell, wlroots virtual keyboard/pointer and screencopy; global hotkeys via portal, GNOME Shell or evdev |
| Input backends | Versioned normalized events/caps; X11/portal/GNOME/evdev routes; layout-aware key model; XI2 raw Hotstring/InputHook; physical `scXXX`, evdev `A & B`, hotplug recovery |
| Accessibility/IME | AT-SPI Text/Action/Selection/Value with pending pumping and title-priority; GTK/Qt/Java/LibreOffice/VS Code matrix; IBus commits drive Hotstring/InputHook |
| Interop | `.so` `DllCall` + libffi callbacks, including Float/Double ABI types; D-Bus adapted COM layer |
| Desktop | GTK3 GUI/Menu, notifications, AutoHotkey StatusNotifierItem tray icon and `A_TrayMenu` |
| Developer tooling | VS Code 0.2.1: syntax/run/tasks/diagnostics, gutter breakpoints, DBGp/DAP stepping, paged variables, exceptions, idle Pause and same-PID reconnect |
| Distribution | deb, RPM, tarball, AppImage, VSIX, AUR PKGBUILD, Flatpak manifest and `--pack` |

The authoritative test totals are **1143/1143** X11/headless assertions,
**17/17** native-Wayland assertions and **234/234** XWayland assertions. CI
also runs regular, ASan and TSan-input builds, four distro containers,
no-XWayland, packed-binary acceptance, identity-bound scenarios, a mixed
hotkey/Hotstring/clipboard soak and the first strict official-Windows v2.0.26
differential trace gate.

## Important limits

- AutoHotkey **v2 only**; v1 commands and migration tooling are not included.
- Windows DLLs, IDispatch/SafeArray COM, Win32 Registry and cross-process Win32
  messages do not exist on Linux. Linux-native `.so`, D-Bus, files and desktop
  protocols are used instead.
- Native-Wayland capabilities vary by compositor. wlroots offers the deepest
  direct input path; GNOME/KDE generally require portal, extension, libei or
  evdev/uinput integration.
- The evdev/uinput chain and multi-client `ahk-inputd` broker are implemented.
  Deb/RPM ship an opt-in `root:input 0660` socket-activated service; the tar
  installer offers `--inputd-service`. Add only trusted users to group `input`
  and re-login. AppImage carries the daemon but cannot install a host service.
- Java ATK Wrapper 0.42.1 ignores Value writes despite reporting success, so the
  runtime returns EIO after readback. Calc exposes Table dimensions but its
  virtual cells need an unimplemented Table-cell API. VS Code 1.134 exposes its
  Electron window/Document, but not Monaco source content.
- IBus/libpinyin commit capture is real-VM tested; Fcitx5 currently has protocol
  coverage only, and Flatpak/portal IM visibility still needs a dedicated host.
  The runtime intentionally observes the user's IME instead of replacing it.

See [linux-port.htm](docs-v2/docs/linux-port.htm) for exact parity levels,
backend selection and known differences.

## Build and verify

```bash
git clone --branch linux-port https://github.com/MonoEven/Autohotkey_Linux.git
cd Autohotkey_Linux
cmake -S . -B build-core
cmake --build build-core -j2
bash tests/run_tests.sh build-core/source/linux/core/ahk_core
bash tests/doccheck/run_check.sh --xvfb build-core/source/linux/core/ahk_core
xvfb-run -a bash tests/oracle/run_x11_oracle.sh build-core/source/linux/core/ahk_core
```

Release packages are built with:

```bash
VER=2.0.26-linux.19 bash tools/linux/pack.sh
bash tools/linux/pack-rpm.sh 2.0.26-linux.19
bash tools/linux/pack-appimage.sh 2.0.26-linux.19
```

Release-tag packages carry Sigstore/GitHub build provenance. After downloading
an asset, verify it against this repository:

```bash
gh attestation verify <downloaded-package> --repo MonoEven/Autohotkey_Linux
```

See [SECURITY.md](SECURITY.md) for the checksum, attestation and OpenPGP trust
policy.

## Project links

- [Linux documentation](https://monoeven.github.io/Autohotkey_Linux/)
- [Capabilities and design](docs-v2/docs/linux-port.htm)
- [Scenario support matrix](tests/scenarios/SUPPORT_MATRIX.md)
- [Engineering audit history](audits/README.md)
- [Security and release provenance](SECURITY.md)
- [Issues](https://github.com/MonoEven/Autohotkey_Linux/issues)
- [Windows upstream](https://www.autohotkey.com/)

Maintained by [MonoEven](https://github.com/MonoEven). Licensed under the
[GNU General Public License](LICENSE).
