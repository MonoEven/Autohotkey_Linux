#!/bin/sh
# AutoHotkey v2 Linux port - RPM builder.
#
# Usage: pack-rpm.sh [version]
#
# Produces dist/autohotkey-linux-<version>-<arch>.rpm using rpmbuild.
set -u

REPO_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$REPO_DIR" || exit 1

VER="${1:-2.0.26-linux.2}"
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
if [ ! -x "$CORE" ]; then
  echo "pack-rpm.sh: $CORE not found; build first" >&2
  exit 1
fi

RPMROOT=$(mktemp -d /tmp/ahk-rpm.XXXXXX)
mkdir -p "$RPMROOT/BUILD" "$RPMROOT/RPMS" "$RPMROOT/SOURCES" "$RPMROOT/SPECS" \
         "$RPMROOT/BUILDROOT"

# tarball source (rpmbuild needs a source archive).
SRC="autohotkey-linux-$VER.tar.gz"
tar czf "$RPMROOT/SOURCES/$SRC" \
  -C "$(dirname "$CORE")" ahk_core \
  -C "$REPO_DIR" docs-v2 README.md LICENSE 2>/dev/null || true

cat > "$RPMROOT/SPECS/autohotkey-linux.spec" <<EOF
Name:           autohotkey-linux
Version:        ${VER%%-*}
Release:        1%{?dist}
Summary:        AutoHotkey v2 Linux port (X11/Wayland)
License:        GPL-2.0-only
URL:            https://github.com/MonoEven/Autohotkey_Linux
Source0:        $SRC
BuildArch:      $RPM_ARCH
Requires:       libX11, libXext, libXrandr, libXinerama, libXtst, gtk3, dbus-libs, libffi

%description
AutoHotkey is a free, open source macro-creation and automation utility
driven by a custom scripting language with special provision for defining
keyboard shortcuts.  This is the Linux port of AutoHotkey v2.0.26:
AutoHotkey v2 syntax only (v1 is not supported), full X11 backend,
GTK3-based Gui/GuiControl/Menu support, D-Bus COM support, and a native
Wayland backend.

%prep
%setup -q -c -n ahk-$VER

%build
# nothing to build: binary is shipped as-is

%install
rm -rf %{buildroot}
install -d %{buildroot}%{_bindir}
install -d %{buildroot}%{_datadir}/autohotkey
install -d %{buildroot}%{_docdir}/autohotkey
install -m 0755 $CORE %{buildroot}%{_datadir}/autohotkey/ahk_core
cat > %{buildroot}%{_bindir}/ahk <<EOS
#!/bin/sh
exec %{_datadir}/autohotkey/ahk_core \\"\\$@\\"
EOS
chmod 0755 %{buildroot}%{_bindir}/ahk
cp -r docs-v2 %{buildroot}%{_docdir}/autohotkey/
install -m 0644 README.md %{buildroot}%{_docdir}/autohotkey/README.md
install -m 0644 LICENSE %{buildroot}%{_docdir}/autohotkey/LICENSE

%files
%{_bindir}/ahk
%{_datadir}/autohotkey/ahk_core
%{_docdir}/autohotkey/*
%license LICENSE

%changelog
* $(date +'%a %b %d %Y') MonoEven <MonoEven@users.noreply.github.com> - ${VER%%-*}-1
- Initial Linux port package.
EOF

rpmbuild --define "_topdir $RPMROOT" -bb "$RPMROOT/SPECS/autohotkey-linux.spec" >/dev/null 2>&1
RC=$?
if [ $RC -ne 0 ]; then
  echo "pack-rpm.sh: rpmbuild failed" >&2
  rm -rf "$RPMROOT"
  exit 1
fi

OUT=dist/autohotkey-linux-$VER-$RPM_ARCH.rpm
mkdir -p dist
cp "$RPMROOT"/RPMS/*/*.rpm "$OUT"
rm -rf "$RPMROOT"
echo "built: $OUT"
ls -la "$OUT"
