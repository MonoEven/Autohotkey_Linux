# Random

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_math.ahk:30](../../tests/doccheck/assert_math.ahk#L30)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Generates a pseudo-random number.

## Syntax

````text
N := Random(A, B)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "ATan=" ATan(0)
MsgBox "ATan2=" ATan(0)
MsgBox "Random_in_range=" (Random(1, 10) >= 1 && Random(1, 10) <= 10)
MsgBox "Random_seed=" (Random(5, 5))
MsgBox "IsNumber=" IsNumber("3.5")
MsgBox "IsInteger=" IsInteger(3)
````

## Upstream reference example

Source: [docs-v2/docs/lib/Random.htm](../../docs-v2/docs/lib/Random.htm)

````ahk
N := Random(1, 10)
````
