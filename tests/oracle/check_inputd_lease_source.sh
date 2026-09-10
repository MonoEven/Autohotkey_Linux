#!/bin/bash
# Static guard for the broker's independent grab lease (Audit47 I01).
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
SRC="$ROOT/source/linux/inputd/inputd.c"

# The lease exists, is armed per grabbed device, and releases the grab itself.
grep -q 'struct device_lease' "$SRC"
grep -q 'static int lease_arm' "$SRC"
grep -q 'lease_arm(slot, fd)' "$SRC"
grep -q 'static void lease_child' "$SRC"
grep -q 'ioctl(dev_fd, EVIOCGRAB, 0);' "$SRC"

# It must be a separate process (fork) with the pipe as its only liveness link,
# otherwise a stopped broker keeps its grab exactly as before.
grep -q 'pid_t pid = fork();' "$SRC"
grep -q 'lease_child_close_others' "$SRC"
grep -q 'prctl(PR_SET_NAME, "ahk-inp-lease"' "$SRC"

# Liveness is driven from the main loop, and losing supervision fails open.
grep -q 'lease_heartbeat();' "$SRC"
grep -q 'grab lease %d lost; releasing every grab' "$SRC"

# A device whose lease cannot be armed must be refused, not held unsupervised.
# The diagnostic is a split string literal, so match its fragments.
grep -q 'independent grab lease ' "$SRC"
grep -q '"unavailable (%s); refusing suppression, "' "$SRC"
grep -q '"listen-only\\n"' "$SRC"

# Leases are cleaned up wherever devices and grabs are torn down.
grep -q 'sLeases\[write_index\] = sLeases\[read_index\];' "$SRC"
grep -q 'lease_stop(i);' "$SRC"
grep -q 'for (int i = 0; i < MAX_DEVICES; ++i)' "$SRC"

# The in-process SIGALRM watchdog stays as a second layer, not the only one.
grep -q 'alarm(2);' "$SRC"

echo 'INPUTD_LEASE_STATIC_PASS fork_lease=1 heartbeat=1 refusal=1 cleanup=1 sigalrm_layer=1'
