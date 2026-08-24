#!/bin/bash
set -u
AHK="${AHK:?runner must export AHK}"
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
rm -f /tmp/scn_evdev_hotplug
if bash "$ROOT/tests/oracle/run_evdev_hotplug_oracle.sh" "$AHK"; then
  touch /tmp/scn_evdev_hotplug
fi
exit 0
