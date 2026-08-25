#!/bin/sh
# AutoHotkey v2 Linux port - RPM builder.
#
# Usage: pack-rpm.sh [version]
#
# Produces dist/autohotkey-linux-<version>-<arch>.rpm using rpmbuild.
# check0820 fix: the source tarball now contains a real ahk-$VER/ top
# directory (the %setup -n ahk-$VER step requires it; before, the archive
# had no such dir and rpmbuild failed on %prep).
set -u

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$REPO_DIR" || exit 1

VER="${1:-2.0.26-linux.18}"
ARCH=$(uname -m)
case "$ARCH" in
  x86_64) RPM_ARCH=x86_64 ;;
  aarch64|arm64) RPM_ARCH=aarch64 ;;
esac

if ! command -v rpmbuild >/dev/null 2>&1; then
  echo "pack-rpm.sh: rpmbuild not found (install rpm-build / rpmdevtools)" >&2
  exit 1
fi

CORE=build-core/source/linux/core/ahk_core
INPUTD=build-core/source/linux/inputd/ahk-inputd
if [ ! -x "$CORE" ]; then
  echo "pack-rpm.sh: $CORE not found; build first" >&2
  exit 1
fi
if [ ! -x "$INPUTD" ]; then
  echo "pack-rpm.sh: $INPUTD not found; build first" >&2
  exit 1
fi

RPMROOT=$(mktemp -d /tmp/ahk-rpm.XXXXXX)
mkdir -p "$RPMROOT/BUILD" "$RPMROOT/RPMS" "$RPMROOT/SOURCES" "$RPMROOT/SPECS" \
         "$RPMROOT/BUILDROOT"

# Source tree: a real ahk-$VER/ directory with the binary + docs.
SRCTREE="$RPMROOT/src/ahk-$VER"
mkdir -p "$SRCTREE"
install -m 0755 "$CORE" "$SRCTREE/ahk_core"
install -m 0755 "$INPUTD" "$SRCTREE/ahk-inputd"
mkdir -p "$SRCTREE/systemd"
cp tools/linux/systemd/ahk-inputd.socket tools/linux/systemd/ahk-inputd.service.in \
  "$SRCTREE/systemd/"
install -m 0644 source/resources/icon_main.ico "$SRCTREE/icon_main.ico"
install -m 0644 docs-v2/docs/static/ahk16.png "$SRCTREE/autohotkey.png"
cp -r docs-v2 "$SRCTREE/docs-v2"
cp -r examples "$SRCTREE/examples"
install -m 0644 README.md "$SRCTREE/README.md"
[ -f LICENSE ] && install -m 0644 LICENSE "$SRCTREE/LICENSE"
# The GNOME Shell extension ships system-wide with the RPM (dpkg-style
# lifecycle: uninstall removes it); %post prints the per-user enable steps.
if [ -d "$REPO_DIR/extension/ahk-global-hotkeys@autohotkey.org" ]; then
  cp -r "$REPO_DIR/extension" "$SRCTREE/extension"
fi
# The full launcher (--update/--uninstall/--check) rendered from the shared
# template; the RPM layout matches /usr with install.sh --prefix /usr, so
# the launcher's install-method detection (rpm/dnf) works on Fedora etc.
# Written into the source tree BEFORE the tar so %install can find it.
sed -e "s|@PREFIX@|/usr|g" \
    -e "s|@LIB_SUB@|share/autohotkey|g" \
    -e "s|@BIN_SUB@|bin|g" \
    -e "s|@DOC_SUB@|share/doc/autohotkey|g" \
    -e "s|@AHK_VERSION@|$VER|g" \
    "$REPO_DIR/tools/linux/ahk-launcher.in" > "$SRCTREE/ahk-launcher"

SRC="autohotkey-linux-$VER.tar.gz"
( cd "$RPMROOT/src" && tar czf "$RPMROOT/SOURCES/$SRC" "ahk-$VER" )

cat > "$RPMROOT/SPECS/autohotkey-linux.spec" <<EOF
Name:           autohotkey-linux
Version:        ${VER%%-*}
Release:        ${VER##*.}.1%{?dist}
Summary:        AutoHotkey v2 automation for Linux (X11/Wayland)
License:        GPL-2.0-only
URL:            https://github.com/MonoEven/Autohotkey_Linux
Source0:        $SRC
BuildArch:      $RPM_ARCH
Requires:       libX11, libXext, libXrandr, libXinerama, libXtst, libxkbcommon-x11, gtk3, dbus-libs, libffi

# No debuginfo: stripping needs eu-strip (elfutils) which CI containers may
# not have; the shipped binary is intentionally as-is (check0820).
%define debug_package %{nil}

%description
AutoHotkey v2.0.26 interpreter and Linux desktop backends: X11/XWayland
automation, native Wayland input routes, GTK3 GUI, AT-SPI controls,
StatusNotifierItem tray, D-Bus and libffi interoperability. AutoHotkey v2
syntax only; v1 is not supported. See the bundled Linux capability matrix
for compositor-specific limits.

%prep
%setup -q -n ahk-$VER

%build
# nothing to build: binary is shipped as-is

%install
rm -rf %{buildroot}
install -d %{buildroot}%{_bindir}
install -d %{buildroot}%{_datadir}/autohotkey
install -d %{buildroot}%{_datadir}/icons/hicolor/16x16/apps
install -d %{buildroot}%{_docdir}/autohotkey
install -d %{buildroot}%{_datadir}/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org
install -d %{buildroot}/usr/lib/systemd/system
install -m 0755 %{_builddir}/ahk-$VER/ahk_core %{buildroot}%{_datadir}/autohotkey/ahk_core
install -m 0755 %{_builddir}/ahk-$VER/ahk-inputd %{buildroot}%{_datadir}/autohotkey/ahk-inputd
install -m 0644 %{_builddir}/ahk-$VER/systemd/ahk-inputd.socket %{buildroot}/usr/lib/systemd/system/ahk-inputd.socket
sed 's|@INPUTD_EXEC@|%{_datadir}/autohotkey/ahk-inputd|g' \
  %{_builddir}/ahk-$VER/systemd/ahk-inputd.service.in \
  > %{buildroot}/usr/lib/systemd/system/ahk-inputd.service
chmod 0644 %{buildroot}/usr/lib/systemd/system/ahk-inputd.service
install -m 0644 %{_builddir}/ahk-$VER/icon_main.ico %{buildroot}%{_datadir}/autohotkey/icon_main.ico
install -m 0644 %{_builddir}/ahk-$VER/autohotkey.png %{buildroot}%{_datadir}/autohotkey/autohotkey.png
install -m 0644 %{_builddir}/ahk-$VER/autohotkey.png %{buildroot}%{_datadir}/icons/hicolor/16x16/apps/autohotkey.png
install -m 0755 %{_builddir}/ahk-$VER/ahk-launcher %{buildroot}%{_bindir}/ahk
cp -r %{_builddir}/ahk-$VER/docs-v2 %{buildroot}%{_docdir}/autohotkey/
cp -r %{_builddir}/ahk-$VER/examples %{buildroot}%{_docdir}/autohotkey/
install -m 0644 %{_builddir}/ahk-$VER/README.md %{buildroot}%{_docdir}/autohotkey/README.md
[ -f %{_builddir}/ahk-$VER/LICENSE ] && install -m 0644 %{_builddir}/ahk-$VER/LICENSE %{buildroot}%{_docdir}/autohotkey/LICENSE
install -m 0644 %{_builddir}/ahk-$VER/extension/ahk-global-hotkeys@autohotkey.org/metadata.json \
  %{buildroot}%{_datadir}/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org/
install -m 0644 %{_builddir}/ahk-$VER/extension/ahk-global-hotkeys@autohotkey.org/extension.js \
  %{buildroot}%{_datadir}/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org/

%post
# Create only the empty authorization group; never grant membership.
if ! getent group input >/dev/null 2>&1 && command -v groupadd >/dev/null 2>&1; then
  groupadd -r input >/dev/null 2>&1 || true
fi
if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload >/dev/null 2>&1 || true
  systemctl try-restart ahk-inputd.socket >/dev/null 2>&1 || true
fi
echo "Optional ahk-inputd broker: systemctl enable --now ahk-inputd.socket"
echo "Then add only trusted users to group input and re-login."
if [ -d %{_datadir}/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org ]; then
  echo
  echo "AutoHotkey GNOME Shell extension installed system-wide."
  echo "To use zero-confirmation global hotkeys on GNOME Wayland, do ONCE"
  echo "per user while logged into the GNOME session:"
  echo "    gnome-extensions enable ahk-global-hotkeys@autohotkey.org"
  echo "then restart GNOME Shell (log out/in, or Alt+F2 and type 'r')."
  echo "(removing the package will delete the extension too.)"
  echo
fi

%preun
if [ "\$1" -eq 0 ] && command -v systemctl >/dev/null 2>&1; then
  systemctl disable --now ahk-inputd.socket >/dev/null 2>&1 || true
  systemctl stop ahk-inputd.service >/dev/null 2>&1 || true
fi

%postun
if command -v systemctl >/dev/null 2>&1; then
  systemctl daemon-reload >/dev/null 2>&1 || true
fi
if [ "\$1" -eq 0 ]; then
  rm -f /run/ahk-inputd.sock /run/ahk-inputd.sock.lock >/dev/null 2>&1 || true
fi

%files
%{_bindir}/ahk
%{_datadir}/autohotkey/ahk_core
%{_datadir}/autohotkey/ahk-inputd
/usr/lib/systemd/system/ahk-inputd.socket
/usr/lib/systemd/system/ahk-inputd.service
%{_datadir}/autohotkey/icon_main.ico
%{_datadir}/autohotkey/autohotkey.png
%{_datadir}/icons/hicolor/16x16/apps/autohotkey.png
%{_datadir}/gnome-shell/extensions/ahk-global-hotkeys@autohotkey.org/*
%{_docdir}/autohotkey/*
%license LICENSE

%changelog
* $(date +'%a %b %d %Y') MonoEven <MonoEven@users.noreply.github.com> - ${VER%%-*}-${VER##*.}.1
- Linux port release with socket-activated ahk-inputd broker.
EOF

# A private rpmdb in the temp tree avoids touching /var/lib/rpm (which is
# often unwritable on CI containers / immutable systems; rpm-4.20 open the
# sqlite db -> "Operation not permitted", check0820).
mkdir -p "$RPMROOT/db"
rpmbuild --define "_topdir $RPMROOT" --define "_dbpath $RPMROOT/db" \
  -bb "$RPMROOT/SPECS/autohotkey-linux.spec" >/tmp/ahk_rpmbuild.log 2>&1
RC=$?
if [ $RC -ne 0 ]; then
  echo "pack-rpm.sh: rpmbuild failed" >&2
  tail -20 /tmp/ahk_rpmbuild.log >&2
  rm -rf "$RPMROOT"
  exit 1
fi

mkdir -p dist
OUT=dist/autohotkey-linux-$VER-$RPM_ARCH.rpm
cp "$RPMROOT"/RPMS/*/*.rpm "$OUT"
# Build-time sanity BEFORE the RPMROOT goes away: the payload must carry
# the full launcher (with the rpm install-method branch) and the GNOME
# extension (both staged into $SRCTREE above).
if [ ! -f "$SRCTREE/ahk-launcher" ] \
   || ! grep -q -- '--update' "$SRCTREE/ahk-launcher" \
   || ! grep -q 'RPM package (rpm/dnf)' "$SRCTREE/ahk-launcher"; then
  echo "pack-rpm.sh: launcher in the source tree is not the full launcher" >&2
  rm -rf "$RPMROOT"
  exit 1
fi
if [ ! -x "$SRCTREE/ahk-inputd" ] \
   || [ ! -f "$SRCTREE/systemd/ahk-inputd.socket" ] \
   || [ ! -f "$SRCTREE/systemd/ahk-inputd.service.in" ]; then
  echo "pack-rpm.sh: ahk-inputd daemon/systemd assets missing from source tree" >&2
  rm -rf "$RPMROOT"
  exit 1
fi
if [ ! -f "$SRCTREE/extension/ahk-global-hotkeys@autohotkey.org/metadata.json" ]; then
  echo "pack-rpm.sh: GNOME extension missing from the source tree" >&2
  rm -rf "$RPMROOT"
  exit 1
fi
rm -rf "$RPMROOT"
echo "built: $OUT"
ls -la "$OUT"