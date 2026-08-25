# MonitorGet

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_monitor.ahk:22](../../tests/doccheck/assert_monitor.ahk#L22)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Checks if the specified monitor exists and optionally retrieves its bounding coordinates.

## Syntax

````text
ActualN := MonitorGet(N, &Left, &Top, &Right, &Bottom)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("mon_primary=" (MonitorGetPrimary() = 1 ? 1 : 0))
Log("mon_name=" (MonitorGetName(1) = "screen" ? 1 : 0))
MonitorGet(1, &ml, &mt, &mr, &mb)
Log("mon_get=" (ml = 0 && mt = 0 && mr = 1024 && mb = 768 ? 1 : 0))
Log("mon_get_ret=" (MonitorGet(1) = 1 ? 1 : 0))
MonitorGet(, &ml2, &mt2, &mr2, &mb2) ; N omitted -> primary monitor.
````

## Upstream reference example

Source: [docs-v2/docs/lib/MonitorGet.htm](../../docs-v2/docs/lib/MonitorGet.htm)

````ahk
try
{
    MonitorGet 2, &Left, &Top, &Right, &Bottom
    MsgBox "Left: " Left " -- Top: " Top " -- Right: " Right " -- Bottom: " Bottom
}
catch
    MsgBox "Monitor 2 doesn't exist or an error occurred."
````
