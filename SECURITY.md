# Security policy

## Reporting a vulnerability

Please report security-sensitive issues privately through GitHub's
[security advisory form](https://github.com/MonoEven/Autohotkey_Linux/security/advisories/new).
Do not include credentials, private scripts or device identifiers in a public
issue.

## Release provenance

GitHub Artifact Attestations are the primary release trust chain. Release-tag
builds are attested by `actions/attest@v4`; for this public repository the
signature uses Sigstore's Public Good instance and is recorded in its public
transparency log. The attestation binds each package digest to this repository,
workflow and commit (SLSA Build Level 2 provenance).

Verify a downloaded package with GitHub CLI:

```bash
gh attestation verify autohotkey-linux-<version>-<arch>.<package> \
  --repo MonoEven/Autohotkey_Linux
```

`CKSUMS.txt` remains the offline integrity manifest, but a checksum downloaded
from the same location as a package is not by itself an independent signature.
Verify both the checksum and the attestation when provenance matters.

## OpenPGP policy

The build must never generate a throwaway OpenPGP identity. OpenPGP signing is
disabled until the maintainer configures both a long-term private key and its
pinned full fingerprint through `AHK_RELEASE_SIGNING_KEY` and
`AHK_RELEASE_SIGNING_FINGERPRINT`.

Without those values, packaging emits `UNSIGNED.txt` and records
`OpenPGP status: UNSIGNED` in `CKSUMS.txt`. If long-term signing is enabled in
the future, this file will publish the fingerprint and an independently hosted
public-key retrieval path before any signature is treated as trusted. A public
key bundled beside the artifact it allegedly authenticates is not a trust
anchor.

The historical linux.15/linux.16 releases retain their original
`CKSUMS.txt.asc` and bundled public-key assets for reproducibility, but those
self-contained ephemeral-key signatures must not be treated as independent
identity proof.

## Privileged input backends

The evdev/uinput backend can read or suppress physical input when granted
`input`/`uinput` permissions. Install only the documented udev/polkit rules,
keep the panic escape sequence available, and review scripts before granting
those permissions. Portal and desktop-extension backends have different
security boundaries; see the Linux capability matrix before deployment.
