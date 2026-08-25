# ImeStatus

- Linux status: `IMPL` (P2)
- Example kind: `verified`
- Environment profile: `desktop-session`
- Verified source: [tests/doccheck/assert_ime.ahk:29](../../tests/doccheck/assert_ime.ahk#L29)
- Profile command: `bash tests/oracle/run_gui_host_matrix.sh "$BIN"`
- Linux adaptation: Linux extension: reports engine/preedit/listener state and IME commit counters; no Windows v2 counterpart

Returns live information about the Linux input-method framework and committed-text listener.

## Syntax

````text
Status := ImeStatus()
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Log("ime_group_int=" (g ~= "^-?[0-9]+$" && gi >= -1 && gi <= 8 ? 1 : 0))

status := ImeStatus()
Log("ime_status_obj=" (Type(status) = "Object" ? 1 : 0))
Log("ime_status_identity=" ((status.Framework = "ibus" || status.Framework = "fcitx5" || status.Framework = "none") && Type(status.Engine) = "String" ? 1 : 0))
Log("ime_status_flags=" ((status.Preedit = 0 || status.Preedit = 1) && (status.Listening = 0 || status.Listening = 1) && (status.Scope = "eavesdrop" || status.Scope = "state-only" || status.Scope = "none") ? 1 : 0))
````

## Upstream reference example

Source: [docs-v2/docs/lib/ImeStatus.htm](../../docs-v2/docs/lib/ImeStatus.htm)

````ahk
s := ImeStatus()
ToolTip s.Framework " / " s.Engine " / preedit=" s.Preedit
````
