#!/bin/sh
# **THE ROOT-OWNED HOOK'S WRAPPER**: tools/shale-gate.sh for a root install
# (tools/install_root.sh copies it to /usr/local/lib/shale/shale-gate.sh).
# It runs the gate beside it, /usr/local/lib/shale/bin/shale-gate, and no
# other: no SHALE_GATE and no HOME pick the program, so nothing a user's
# account can write chooses what the hook runs. Anything but the gate's pass
# (0) or its refusal (2) is a refusal: a missing or broken gate refuses, it
# never passes.
bin=/usr/local/lib/shale/bin/shale-gate
"$bin"
rc=$?
[ "$rc" -eq 0 ] && exit 0
[ "$rc" -eq 2 ] && exit 2
echo "shale-gate: $bin failed (exit $rc), so the call is refused" >&2
exit 2
