# IsSetRef

- Linux status: `IMPL` (P1)
- Example kind: `verified`
- Environment profile: `x11`
- Verified source: [tests/doccheck/assert_misc_cov.ahk:84](../../tests/doccheck/assert_misc_cov.ahk#L84)
- Profile command: `bash tests/doccheck/run_check.sh --xvfb "$BIN"`

## Linux-verified example excerpt

This excerpt is executed by the profile command above; surrounding setup and assertions remain in the linked source.

````ahk
Check("isset_set", () => (probe_assigned := 1, IsSet(probe_assigned)))
RefCheck(&p) {
    return IsSetRef(&p)
}
Check("issetref_unset", () => IsSetRef(&probe_ref_unset))
Check("issetref_set", () => (probe_ref_set := 5, IsSetRef(&probe_ref_set)))
````
