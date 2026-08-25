# MonitorGetWorkArea

- Linux status: `IMPL` (P3)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_monitor.ahk:27](../../tests/doccheck/assert_monitor.ahk#L27)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: Approximate via XRandR/Xinerama (single-screen fallback)

Checks if the specified monitor exists and optionally retrieves the bounding coordinates of its working area.

## Syntax

````text
ActualN := MonitorGetWorkArea(N, &Left, &Top, &Right, &Bottom)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MonitorGet(, &ml2, &mt2, &mr2, &mb2) ; N omitted -> primary monitor.
Log("mon_get_primary=" (ml2 = 0 && mt2 = 0 && mr2 = 1024 && mb2 = 768 ? 1 : 0))
MonitorGetWorkArea(1, &wl, &wt, &wr, &wb)
Log("mon_workarea=" (wl = 0 && wt = 0 && wr = 1024 && wb = 768 ? 1 : 0))
try
    MonitorGet(2)
````

## Upstream reference example

Source: [docs-v2/docs/lib/MonitorGetWorkArea.htm](../../docs-v2/docs/lib/MonitorGetWorkArea.htm)

The upstream page has no standalone code block; use the Linux-verified excerpt above.
