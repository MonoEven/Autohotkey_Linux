# AHK v2 Syntax Studio

`syntax_studio.ahk` is a complex, runnable syntax-teaching GUI for the Linux port.
It uses the GUI controls covered by the Linux GUI regression suite.

## Run

```bash
ahk examples/gui/syntax_studio.ahk
```

The current VM build was tested at 1280x800 with an X11/GTK3 display. The
application opens at 1220x770 and provides:

- ten lessons covering variables, conditions, loops, functions, objects,
  exceptions, hotkeys, hotstrings/InputHook, GUI events, and files/processes;
- course filtering by level or search text;
- syntax cards and runnable examples;
- an editable practice pad;
- Run, Check, Reset, Copy, Previous, and Next actions;
- a syntax concept tree, checklist, activity log, and status bar.

## VM Evidence

The following screenshots are captured from the real VM GUI after the final
interaction pass:

- `screenshots/syntax_studio_overview.png`: all ten lessons and lesson 1;
- `screenshots/syntax_studio_conditions.png`: lesson 2 selected;
- `screenshots/syntax_studio_loops_filter.png`: `loop` search filtered the map to lesson 3;
- `screenshots/syntax_studio_check.png`: lesson 2 practice check passed;
- `screenshots/syntax_studio_run.png`: lesson 2 practice run completed with exit code 0.

The VM interaction log records window discovery, lesson selection, search filtering,
Check, Run, and the generated exercise file.
