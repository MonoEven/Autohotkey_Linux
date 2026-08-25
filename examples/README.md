# AutoHotkey Linux examples

This directory covers every script-visible Linux function currently classified
`IMPL`. The source of truth is `tests/doccheck/worklist.tsv`, not a hand-written
list which can silently drift.

## Coverage layers

1. `FUNCTION_COVERAGE.md` maps every IMPL entry to its generated page, least
   required environment and exact Linux-verified source line.
2. `generated/<Function>.md` provides syntax, a Linux-executed excerpt, the
   command which runs it, parity/adaptation notes and the upstream code sample
   when one exists.
3. `catalog.json` is the machine-readable schema-1 catalog used by CI and tools.
4. Curated scripts under `language/`, `gui/`, `interactive/`, `lifecycle/` and
   `safety/` make type pages and non-call statements useful outside the test
   harness.
5. Existing `tests/doccheck/assert_*.ahk` files remain the executable evidence.
   They are linked rather than copied, so an example cannot pass while its actual
   assertion diverges.

Current generated totals are shown at the top of `FUNCTION_COVERAGE.md` and are
checked against the live worklist. An upstream documentation snippet alone does
not count as Linux coverage.

## Run examples

Build the core first, then:

```bash
# Fast, unattended curated examples plus generated-catalog drift check.
bash examples/run.sh all-curated build-core/source/linux/core/ahk_core

# Every X11/headless Linux-verified source backing the catalog.
bash examples/run.sh all-verified build-core/source/linux/core/ahk_core

# Native-Wayland and real desktop-session profiles.
bash examples/run.sh wayland build-core/source/linux/core/ahk_core
bash examples/run.sh desktop-session build-core/source/linux/core/ahk_core

# Interactive only: opens an InputBox.
bash examples/run.sh interactive build-core/source/linux/core/ahk_core
```

`Shutdown` is a safety-boundary example. Its default run exits with refusal and
never changes system state. It accepts only the explicit
`--i-understand-logoff` acknowledgement, which must not be used without saving
work. No automated profile opts in.

## Regenerate

```bash
python3 tools/gen_examples_catalog.py
python3 tools/gen_examples_catalog.py --check
```

The generator fails if any IMPL entry lacks an executable call or curated
mapping, if a source line disappears, if names duplicate, or if generated files
are stale. CI runs both the catalog check and all unattended curated examples.
