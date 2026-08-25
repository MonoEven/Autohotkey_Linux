# Exit

- Linux status: `IMPL` (P1)
- Example kind: `lifecycle`
- Environment profile: `lifecycle`
- Verified source: [examples/lifecycle/exit_current_thread.ahk:5](../../examples/lifecycle/exit_current_thread.ahk#L5)
- Profile command: `bash examples/run.sh lifecycle "$BIN"`

Exits the current thread.

## Syntax

````text
Exit ExitCode
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
out := A_Args.Length ? A_Args[1] : "/tmp/ahk-example-exit.txt"
FileAppend("before-exit`n", out)
Exit 7
FileAppend("unreachable`n", out)
````

## Upstream reference example

Source: [docs-v2/docs/lib/Exit.htm](../../docs-v2/docs/lib/Exit.htm)

````ahk
#z::
{
    call_exit
    MsgBox "This MsgBox will never happen because of the Exit."
    call_exit()
    {
        Exit ; Terminate this function as well as the calling function.
    }
}
````
