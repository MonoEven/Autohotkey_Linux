#!/bin/bash
# Packed-runtime capability manifest oracle (Audit47 §13.2).
#
# A libei-enabled interactive runtime cannot be copied into a standalone ELF
# (the dynamic loader resolves DT_NEEDED before embedded resources can be
# extracted), so --pack embeds the feature-off ahk_core_pack template.  That
# template has fewer optional capabilities than the runtime doing the packing,
# and before linux.23 the difference was only discovered after deployment.
#
# This oracle proves the new contract end to end:
#   * --pack prints an explicit capability-loss notice when the template lacks
#     a lane this runtime has;
#   * the produced executable embeds a manifest naming the release and
#     capabilities it actually contains;
#   * the packed binary reports that manifest through --version/--pack-info/
#     --diag, and a script sees the same answer through
#     HotkeyBackendGet().libei_build_enabled;
#   * arguments are forwarded to the embedded script (A_Args) and A_IsCompiled
#     is 1 for the whole packed process, with /script PATH as the override.
#
# Usage: run_pack_capability_oracle.sh [FULL_CORE] [TEMPLATE_CORE]
# Exit codes: 0 pass, 1 assertion failure, 2 required binary absent.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
OUT="$ROOT/tests/oracle/out"
mkdir -p "$OUT" || exit 1

FULL="${1:-$ROOT/build-core/source/linux/core/ahk_core}"
TMPL="${2:-$ROOT/build-pack-runtime/source/linux/core/ahk_core}"
case "$FULL" in /*) ;; *) FULL="$ROOT/$FULL" ;; esac
case "$TMPL" in /*) ;; *) TMPL="$ROOT/$TMPL" ;; esac

WORK=/tmp/ahk-pack-capability

cleanup() { rm -rf "$WORK"; }
die() { echo "PACK_CAPABILITY_FAIL step=$1 detail=$2" >&2; cleanup; exit 1; }
skip() { echo "PACK_CAPABILITY_SKIP missing=$1"; cleanup; exit 2; }

[ -x "$FULL" ] || skip "$FULL"
[ -x "$TMPL" ] || skip "$TMPL"
command -v strings >/dev/null 2>&1 || skip "strings(1)"

rm -rf "$WORK"
mkdir -p "$WORK" || die 0 "cannot create $WORK"

info() { # <binary> <key> -> value (empty when absent)
  "$1" --pack-info 2>/dev/null | sed -n "s/^$2=//p" | head -1
}

# --- 0. both binaries must be able to declare their capabilities ------------

for pair in "full:$FULL" "template:$TMPL"; do
  name=${pair%%:*}
  bin=${pair#*:}
  schema=$(info "$bin" schema)
  [ "$schema" = "1" ] \
    || die 0 "$name binary ($bin) does not answer --pack-info with schema=1 (got '$schema')"
  [ -n "$(info "$bin" port_version)" ] \
    || die 0 "$name binary reports no port_version"
  libei=$(info "$bin" libei)
  [ "$libei" = "0" ] || [ "$libei" = "1" ] \
    || die 0 "$name binary reports libei='$libei', expected 0 or 1"
done

FULL_VER=$(info "$FULL" port_version)
FULL_LIBEI=$(info "$FULL" libei)
TMPL_VER=$(info "$TMPL" port_version)
TMPL_LIBEI=$(info "$TMPL" libei)
TMPL_PORTAL=$(info "$TMPL" portal)

echo "=== Packed capability manifest: full=$FULL_VER(libei=$FULL_LIBEI) template=$TMPL_VER(libei=$TMPL_LIBEI) ==="

# --- 1. pack, and assert the capability-loss notice when it applies ---------

cat >"$WORK/probe.ahk" <<'EOF'
#Requires AutoHotkey v2.0
out := A_Args[1]
hb := HotkeyBackendGet()
FileAppend("compiled=" A_IsCompiled "`n", out)
FileAppend("args=" A_Args.Length "`n", out)
FileAppend("arg2=" (A_Args.Length >= 2 ? A_Args[2] : "") "`n", out)
FileAppend("libei_build=" hb.libei_build_enabled "`n", out)
if hb.libei_build_enabled
    ExitApp(7)
ExitApp(0)
EOF

AUDIT="$WORK/audit.ahk"
cat >"$AUDIT" <<'EOF'
#Requires AutoHotkey v2.0
FileAppend("audit-ran`n", A_Args[1])
ExitApp(0)
EOF

PACKED="$WORK/packed"
AHK_PACK_RUNTIME="$TMPL" "$FULL" --pack "$PACKED" "$WORK/probe.ahk" \
  >"$WORK/pack.out" 2>"$WORK/pack.err"
pack_rc=$?
[ "$pack_rc" = 0 ] || { cat "$WORK/pack.err" >&2; die 1 "--pack exited $pack_rc"; }
[ -x "$PACKED" ] || die 1 "packed executable missing"

capability_loss=0
if [ "$FULL_LIBEI" = "1" ] && [ "$TMPL_LIBEI" = "0" ]; then
  capability_loss=1
  grep -q 'libei/EIS injection but the pack template was not' "$WORK/pack.err" \
    || { cat "$WORK/pack.err" >&2; die 1 "no capability-loss notice for a libei runtime packed with a feature-off template"; }
  grep -q 'HotkeyBackendGet().libei_build_enabled' "$WORK/pack.err" \
    || die 1 "the capability-loss notice does not tell the user how to observe it"
else
  # Both runtimes declare the same libei capability: there is nothing to warn
  # about, and a notice would be noise.  Asserted, not silently tolerated.
  if grep -q 'pack template was not' "$WORK/pack.err"; then
    cat "$WORK/pack.err" >&2
    die 1 "capability-loss notice printed although both runtimes declare libei=$TMPL_LIBEI"
  fi
fi
if [ "$FULL_VER" != "$TMPL_VER" ]; then
  grep -q "is release $TMPL_VER while this runtime is $FULL_VER" "$WORK/pack.err" \
    || die 1 "mixed-release pack (-packer $FULL_VER, template $TMPL_VER) was not reported"
fi

# --- 2. the manifest must be embedded, not synthesized at run time ----------

strings -a "$PACKED" | grep -qx 'schema=1' \
  || die 2 "no manifest keys found in the packed ELF"
grep -aq $'\001ahk-pack-manifest' "$PACKED" \
  || die 2 "the reserved manifest resource name is absent from the packed ELF"

# --- 3. the packed binary reports the template's facts ----------------------

for pair in "port_version:$TMPL_VER" "schema:1" "libei:$TMPL_LIBEI" "portal:$TMPL_PORTAL"; do
  key=${pair%%:*}
  want=${pair#*:}
  got=$(info "$PACKED" "$key")
  [ "$got" = "$want" ] || die 3 "--pack-info $key on the packed binary is '$got', expected '$want'"
done

ver_out=$("$PACKED" --version 2>&1)
case "$ver_out" in
  *"v$TMPL_VER"*) ;;
  *) die 3 "--version of the packed binary does not name the embedded release: $ver_out" ;;
esac

diag=$("$PACKED" --diag 2>&1)
printf '%s\n' "$diag" | grep -q "^packed-port_version=$TMPL_VER$" \
  || die 3 "--diag does not report packed-port_version=$TMPL_VER"
printf '%s\n' "$diag" | grep -q "^packed-libei=$TMPL_LIBEI$" \
  || die 3 "--diag does not report packed-libei=$TMPL_LIBEI"
printf '%s\n' "$diag" | grep -q "^packed-packer_libei=$FULL_LIBEI$" \
  || die 3 "--diag does not report the packing runtime's capability (packer_libei=$FULL_LIBEI)"

# --- 4. a script sees the same capability answer ----------------------------

SCRIPT_OUT="$WORK/script.out"
rm -f "$SCRIPT_OUT"
"$PACKED" "$SCRIPT_OUT" hello >"$WORK/script.log" 2>&1
script_rc=$?
[ -f "$SCRIPT_OUT" ] || { cat "$WORK/script.log" >&2; die 4 "the packed binary produced no script output"; }
grep -q '^compiled=1$' "$SCRIPT_OUT" \
  || { cat "$SCRIPT_OUT" >&2; die 4 "A_IsCompiled is not 1 in a packed process"; }
grep -q '^args=2$' "$SCRIPT_OUT" \
  || { cat "$SCRIPT_OUT" >&2; die 4 "arguments were not forwarded to the embedded script"; }
grep -q '^arg2=hello$' "$SCRIPT_OUT" \
  || { cat "$SCRIPT_OUT" >&2; die 4 "A_Args[2] is not the first user argument"; }
grep -q "^libei_build=$TMPL_LIBEI$" "$SCRIPT_OUT" \
  || { cat "$SCRIPT_OUT" >&2; die 4 "the script's libei_build_enabled does not match the manifest ($TMPL_LIBEI)"; }
[ "$script_rc" = 0 ] \
  || die 4 "the script exited $script_rc: it observed a capability the manifest denies"

# --- 5. "--" separator and the /script override -----------------------------

SEP_OUT="$WORK/separator.out"
rm -f "$SEP_OUT"
"$PACKED" -- "$SEP_OUT" world >"$WORK/separator.log" 2>&1 \
  || { cat "$WORK/separator.log" >&2; die 5 "'--' argument forwarding failed"; }
grep -q '^arg2=world$' "$SEP_OUT" \
  || { cat "$SEP_OUT" >&2; die 5 "'--' did not forward the argument"; }

AUDIT_OUT="$WORK/audit.out"
rm -f "$AUDIT_OUT"
"$PACKED" /script "$AUDIT" "$AUDIT_OUT" >"$WORK/audit.log" 2>&1 \
  || { cat "$WORK/audit.log" >&2; die 5 "'/script PATH' override failed"; }
grep -q '^audit-ran$' "$AUDIT_OUT" \
  || { cat "$AUDIT_OUT" >&2; die 5 "'/script PATH' did not run the requested script"; }

# A plain (unpacked) runtime must not claim to be compiled.
PLAIN_OUT="$WORK/plain.out"
rm -f "$PLAIN_OUT"
"$FULL" "$WORK/probe.ahk" "$PLAIN_OUT" x >"$WORK/plain.log" 2>&1
grep -q '^compiled=0$' "$PLAIN_OUT" \
  || { cat "$PLAIN_OUT" >&2; die 5 "the unpacked runtime reports A_IsCompiled != 0"; }

# --- summary ---------------------------------------------------------------

case "$capability_loss" in
  1) loss_json=true ;;
  *) loss_json=false ;;
esac
cat >"$OUT/pack-capability-summary.json" <<EOF
{"schema":1,"result":"pass","full_runtime_version":"$FULL_VER","full_runtime_libei":$FULL_LIBEI,"template_version":"$TMPL_VER","template_libei":$TMPL_LIBEI,"template_portal":$TMPL_PORTAL,"capability_loss_notice":$loss_json,"manifest_embedded":true,"pack_info_matches_template":true,"diag_reports_manifest":true,"script_observes_manifest":true,"args_forwarded":true,"separator_forwarded":true,"script_override":true,"unpacked_is_not_compiled":true}
EOF

cleanup
echo "PACK_CAPABILITY_ORACLE_PASS template=$TMPL_VER/$TMPL_LIBEI capability_loss_notice=$loss_json embedded=1 args=1 override=1"
