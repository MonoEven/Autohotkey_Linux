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

VER="${1:-2.0.26-linux.6}"
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

# Source tree: a real ahk-$VER/ directory with the binary + docs.
SRCTREE="$RPMROOT/src/ahk-$VER"
mkdir -p "$SRCTREE"
install -m 0755 "$CORE" "$SRCTREE/ahk_core"
cp -r docs-v2 "$SRCTREE/docs-v2"
install -m 0644 README.md "$SRCTREE/README.md"
[ -f LICENSE ] && install -m 0644 LICENSE "$SRCTREE/LICENSE"

SRC="autohotkey-linux-$VER.tar.gz"
( cd "$RPMROOT/src" && tar czf "$RPMROOT/SOURCES/$SRC" "ahk-$VER" )

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

# No debuginfo: stripping needs eu-strip (elfutils) which CI containers may
# not have; the shipped binary is intentionally as-is (check0820).
%define debug_package %{nil}

%description
AutoHotkey is a free, open source macro-creation and automation utility
driven by a custom scripting language with special provision for defining
keyboard shortcuts.  This is the Linux port of AutoHotkey v2.0.26:
AutoHotkey v2 syntax only (v1 is not supported), full X11 backend,
GTK3-based Gui/GuiControl/Menu support, D-Bus COM support, and a native
Wayland backend.

%prep
%setup -q -n ahk-$VER

%build
# nothing to build: binary is shipped as-is

%install
rm -rf %{buildroot}
install -d %{buildroot}%{_bindir}
install -d %{buildroot}%{_datadir}/autohotkey
install -d %{buildroot}%{_docdir}/autohotkey
install -m 0755 %{_builddir}/ahk-$VER/ahk_core %{buildroot}%{_datadir}/autohotkey/ahk_core
cat > %{buildroot}%{_bindir}/ahk <<EOS
#!/bin/sh
exec %{_datadir}/autohotkey/ahk_core "\\$@"
EOS
chmod 0755 %{buildroot}%{_bindir}/ahk
cp -r %{_builddir}/ahk-$VER/docs-v2 %{buildroot}%{_docdir}/autohotkey/
install -m 0644 %{_builddir}/ahk-$VER/README.md %{buildroot}%{_docdir}/autohotkey/README.md
[ -f %{_builddir}/ahk-$VER/LICENSE ] && install -m 0644 %{_builddir}/ahk-$VER/LICENSE %{buildroot}%{_docdir}/autohotkey/LICENSE

%files
%{_bindir}/ahk
%{_datadir}/autohotkey/ahk_core
%{_docdir}/autohotkey/*
%license LICENSE

%changelog
* $(date +'%a %b %d %Y') MonoEven <MonoEven@users.noreply.github.com> - ${VER%%-*}-1
- Initial Linux port package.
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
rm -rf "$RPMROOT"
echo "built: $OUT"
ls -la "$OUT"