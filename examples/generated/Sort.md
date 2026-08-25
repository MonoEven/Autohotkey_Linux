# Sort

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `headless`
- Verified source: [tests/doccheck/assert_string.ahk:46](../../tests/doccheck/assert_string.ahk#L46)
- Profile command: `bash tests/doccheck/run_check.sh "$BIN"`

Arranges a variable's contents in alphabetical, numerical, or random order (optionally removing duplicates).

## Syntax

````text
SortedString := Sort(String , Options, Callback)
````

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
MsgBox "Chr=" Chr(65)
MsgBox "Chr_0=" Chr(0)
MsgBox "Sort_comma=" Sort("c,b,a", "D,")
MsgBox "Sort_numeric=" Sort("3,1,2", "N D,")
MsgBox "SplitPath_name=" (SplitPath("/a/b/file.txt", &n, &d, &e, &ne, &dr), n)
MsgBox "SplitPath_dir=" (SplitPath("/a/b/file.txt", &n, &d, &e, &ne, &dr), d)
````

## Upstream reference example

Source: [docs-v2/docs/lib/Sort.htm](../../docs-v2/docs/lib/Sort.htm)

````ahk
MyVar := "5,3,7,9,1,13,999,-4"
MyVar := Sort(MyVar, "N D,")  ; Sort numerically, use comma as delimiter.
MsgBox MyVar   ; The result is -4,1,3,5,7,9,13,999
````
