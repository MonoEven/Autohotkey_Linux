# Windows v2.0.26 differential traces

This directory is the first gated slice of check_detail0824 M6/T2. It compares
observable AutoHotkey semantics, not implementation-private events.

## Provenance

The committed golden was produced by the official AutoHotkey v2.0.26 x64
portable release. `collect_windows.ps1` downloads the archive from the official
GitHub release, verifies the publisher's archive SHA-256, verifies the extracted
`AutoHotkey64.exe` SHA-256, runs the suite at least twice and requires every
trace to be byte-identical. No downloaded executable is committed.

The exact release, archive/executable/trace hashes, collection time and repeat
count are pinned in `golden/windows-v2.0.26-x64.manifest.json`. The initial
golden was run three times and was byte-identical.

## Trace v1

Each JSONL row has exactly these fields:

- `schema`, `seq`: schema version and strict one-based sequence;
- `case`, `kind`: operation group and observation type;
- `vk`, `sc`: Windows-compatible logical VK and canonical set-1 scan code;
- `text`: Unicode scalar for an InputHook character event;
- `name`: Hotkey/Hotstring name or terminal reason.

`trace-schema-v1.json` is the machine-readable row schema. Timestamps and device
IDs are intentionally absent: they are the only current platform whitelist and
cannot mask differences in ordering, VK, SC, Unicode, callback names or terminal
state. `compare_trace.py` also checks the manifest hash before comparing rows.

Current gated cases are:

1. external `a`, Shift+A and physical F13 into InputHook, including exact
   down/char/up order and VK/SC;
2. modified `^F11` and `F12 Up` hotkey activation;
3. dynamic `:B0*:zxq` Hotstring activation and `A_ThisHotkey`.

The Linux sender is an independent XTEST C tool with explicit physical keycodes,
not the Linux runtime's Send implementation. This avoids a same-codebase oracle.

## Reproduce

On Windows PowerShell:

```powershell
./tests/differential/collect_windows.ps1 -Repeat 3
```

On Linux with Xvfb and build-core:

```bash
bash tests/differential/run_linux_trace.sh build-core/source/linux/core/ahk_core
```

The Linux command writes ignored evidence under `tests/differential/out/` and is
a blocking CI gate for this declared slice.

## Explicitly remaining

This does not complete all Windows differential work. Modifier wildcards,
pass-through variants, custom combos/remaps, SendInput/SendPlay timing and the
full Hotstring option matrix still need golden cases. New cases must first be
stable across repeated official-Windows runs; platform-specific fields may only
be ignored by changing this document, the comparator and the manifest together.
