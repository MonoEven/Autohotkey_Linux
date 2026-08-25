# GroupDeactivate

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:244](../../tests/doccheck/assert_misc_cov.ahk#L244)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

Similar to GroupActivate except activates the next window not in the group.

## Syntax

````text
GroupDeactivate GroupName , Mode
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
    GroupAdd("covgrp", "MiscCov Alpha")
    GroupAdd("covgrp", "MiscCov Beta")
    GroupDeactivate("covgrp")
    return 1
}
Check("groupdeact", GroupDeact)
````

## Upstream reference example

Source: [docs-v2/docs/lib/GroupDeactivate.htm](../../docs-v2/docs/lib/GroupDeactivate.htm)

````ahk
GroupDeactivate "MyFavoriteWindows"  ; Visit non-favorite windows to clean up desktop.
````
