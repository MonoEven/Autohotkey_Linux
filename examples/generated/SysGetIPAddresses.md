# SysGetIPAddresses

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_sys.ahk:193](../../tests/doccheck/assert_sys.ahk#L193)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Returns an array of the system's IPv4 addresses.

## Syntax

````text
Addresses := SysGetIPAddresses()
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "SysGet_invalid=" (SysGet(9999) = 0)
; Docs: SysGetIPAddresses returns an Array of IPv4 strings.
ips := SysGetIPAddresses()
MsgBox "SysGetIP_type=" (Type(ips) = "Array")
MsgBox "SysGetIP_len=" (ips.Length >= 1)
has_loopback := 0
````

## Upstream reference example

Source: [docs-v2/docs/lib/SysGetIPAddresses.htm](../../docs-v2/docs/lib/SysGetIPAddresses.htm)

````ahk
addresses := SysGetIPAddresses()
msg := "IP addresses:`n"
for n, address in addresses
    msg .= address "`n"
MsgBox msg
````
