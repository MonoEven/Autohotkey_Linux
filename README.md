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

**Current release:** [`v2.0.26-linux.16`](https://github.com/MonoEven/Autohotkey_Linux/releases/latest)
· **Docs:** [GitHub Pages](https://monoeven.github.io/Autohotkey_Linux/)
· **Detailed results:** [CHECK_REPORT.md](tests/doccheck/CHECK_REPORT.md)
· **Changelog:** [ChangeLog.htm](docs-v2/docs/ChangeLog.htm)

## Install

Download the package for your system from
[GitHub Releases](https://github.com/MonoEven/Autohotkey_Linux/releases):

```bash
# Debian / Ubuntu
sudo apt install ./autohotkey-linux-2.0.26-linux.16-amd64.deb

# Fedora / openSUSE
sudo dnf install ./autohotkey-linux-2.0.26-linux.16-x86_64.rpm

# AppImage
chmod +x autohotkey-linux-2.0.26-linux.16-x86_64.AppImage
./autohotkey-linux-2.0.26-linux.16-x86_64.AppImage script.ahk

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

## What works

| Area | Linux implementation |
|---|---|
| Language/runtime | AutoHotkey v2 syntax, objects, functions, classes, timers, files and processes |
| X11/XWayland | `Win*`, `Control*`, hotkeys, hotstrings, InputHook, Unicode Send, pixels/monitors, dialogs and GTK3 GUI |
| Native Wayland | xdg-shell, wlroots virtual keyboard/pointer and screencopy; global hotkeys via portal, GNOME Shell or evdev |
| Input backends | Versioned normalized events/caps; X11/portal/GNOME/evdev routes; layout-aware key model; XI2 raw multi-client Hotstring/InputHook; physical `scXXX` and evdev `A & B` |
| Accessibility | AT-SPI Cache text/action controls on GNOME Wayland; real GTK 3.24, Qt 6.9 and VS Code/Electron 1.134 host matrix |
| Interop | `.so` `DllCall` + libffi callbacks, including Float/Double ABI types; D-Bus adapted COM layer |
| Desktop | GTK3 GUI/Menu, notifications, AutoHotkey StatusNotifierItem tray icon and `A_TrayMenu` |
| Developer tooling | VS Code 0.2.0: syntax/run/tasks/diagnostics plus DBGp/DAP breakpoints, paged variables, exceptions, idle Pause and same-PID reconnect |
| Distribution | deb, RPM, tarball, AppImage, VSIX, AUR PKGBUILD, Flatpak manifest and `--pack` |

The authoritative test totals are **1139/1139** X11/headless assertions,
**17/17** native-Wayland assertions and **234/234** XWayland assertions. CI
also runs regular, ASan and TSan-input builds, four distro containers,
no-XWayland, packed-binary acceptance, scenario gates and an RSS/event-count soak.

## Important limits

- AutoHotkey **v2 only**; v1 commands and migration tooling are not included.
- Windows DLLs, IDispatch/SafeArray COM, Win32 Registry and cross-process Win32
  messages do not exist on Linux. Linux-native `.so`, D-Bus, files and desktop
  protocols are used instead.
- Native-Wayland capabilities vary by compositor. wlroots offers the deepest
  direct input path; GNOME/KDE generally require portal, extension, libei or
  evdev/uinput integration.
- The evdev/uinput chain and multi-client `ahk-inputd` broker are implemented;
  privileged installation still needs the supplied udev/polkit policy.
- VS Code 1.134 exposes its Electron window/Document to AT-SPI, but not Monaco
  source content even with forced accessibility; the matrix reports window-only.
- IBus coexistence is tested, but a dedicated AutoHotkey IBus engine is not yet
  implemented. KDE VM and Flatpak-host end-to-end matrices still need dedicated
  environments.

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
VER=2.0.26-linux.16 bash tools/linux/pack.sh
bash tools/linux/pack-rpm.sh 2.0.26-linux.16
bash tools/linux/pack-appimage.sh 2.0.26-linux.16
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
