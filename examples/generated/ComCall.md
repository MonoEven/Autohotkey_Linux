# ComCall

- Linux status: `IMPL` (P4)
- Example kind: `verified-error`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:193](../../tests/doccheck/assert_misc_cov.ahk#L193)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`
- Linux adaptation: No COM vtable on Linux: raises "Invalid arg type." / "Invalid parameter #1" (no runnable implementation)

Calls a native COM interface method by index.

## Syntax

````text
Result := ComCall(Index, ComObj , Type1, Arg1, Type2, Arg2, ReturnType)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
; ComCall: no COM vtable on Linux; doc-style invocation must fail with a
; clear error, never crash.
Check("comcall_err", () => ComCall(0, ComValue(3, 100), "Int"))

; --- Key lookup (headless-safe: static keysym tables) --------------------
Check("getkeyvk", () => GetKeyVK("a") != 0)
````

## Upstream reference example

Source: [docs-v2/docs/lib/ComCall.htm](../../docs-v2/docs/lib/ComCall.htm)

````ahk
/*
  Methods in ITaskbarList's VTable:
    IUnknown:
      0 QueryInterface  -- use ComObjQuery instead
      1 AddRef          -- use ObjAddRef instead
      2 Release         -- use ObjRelease instead
    ITaskbarList:
      3 HrInit
      4 AddTab
      5 DeleteTab
      6 Act
````
