# External input oracle

These tests deliberately keep observation/injection outside `ahk_core` so the
port cannot validate one internal convention with another copy of itself.

`input_oracle.c` has two X11 modes:

- `record-x11`: an XI2.1 raw-event recorder that writes schema-1 JSONL with
  monotonic time, X server time, press/release, keycode, device/source IDs and
  an independent XTEST-device classification;
- `inject-x11`: a separate XTEST client used to drive an AHK hotkey.

`run_x11_oracle.sh` verifies both directions under Xvfb. The checked-in
`verify_trace.py` requires an exact down/up pair and an XI2 source ID belonging
to an XTEST device. Raw traces are test output; the stable schema and summary
make later Linux/Windows differential traces possible without changing the
consumer format.

The physical-layer VM lane uses `tools/linux/uinput-inject.c`: its virtual
keyboard deliberately has no AHK identity, so evdev sees an independent input
device. It remains permission-gated and is not pretended to run on ordinary
GitHub-hosted runners.

Run locally:

```bash
xvfb-run -a bash tests/oracle/run_x11_oracle.sh \
  build-core/source/linux/core/ahk_core
```
