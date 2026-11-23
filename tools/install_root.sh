#!/bin/sh
# tools/install_root.sh: Shale's runtime, its gate and the hook's wrapper,
# installed root's, at /usr/local/lib/shale, where nothing an agent runs can
# write them. The person who owns the box runs it, with sudo; no agent does
# (the gate refuses `sudo`, and `run` refuses it by name).
#
#   shale c/install.shale dry=1     build and check c/out/release (as yourself)
#   sudo tools/install_root.sh      then install what was built (as root)
#
# It installs:
#   /usr/local/lib/shale/bin/shale        the runtime (c/out/release/shale)
#   /usr/local/lib/shale/bin/shale-gate   the gate (c/out/release/shale-gate)
#   /usr/local/lib/shale/shale-gate.sh    the hook's wrapper, which runs that
#                                         gate alone (tools/shale-gate-root.sh)
#   /usr/local/bin/shale                  a link to the runtime, for PATH
#
# Each file is written as `<name>.new` and renamed into place, so a hook or
# a launch that runs meanwhile runs the old file or the new one, never half.
# It never runs what it copies as root: c/out is the user's, so an agent
# could have changed it since the build; the checks it runs (--version, the
# gate on a launch and a refusal) run the installed copies as the user who
# ran sudo. It prints each file's sha256 and the checkout's head, so what
# went in is on record.
#
# What else the switch needs, by hand, once (docs/SHALE-AGENTS.md has the
# lines): the hook command in ~/.claude/settings.json pointed at
# /usr/local/lib/shale/shale-gate.sh; ~/.local/bin/shale removed, so PATH
# finds /usr/local/bin/shale; and the `tools` and `localbin` mount lines made
# ` ro`. To roll back: point the hook back at tools/shale-gate.sh in the
# checkout and run `shale c/install.shale` as before; /usr/local/lib/shale
# and /usr/local/bin/shale can stay or go (`sudo rm -r /usr/local/lib/shale
# /usr/local/bin/shale`).
set -eu
cd "$(dirname "$0")/.."
LIB=/usr/local/lib/shale
[ "$(id -u)" -eq 0 ] || { echo "install_root: run it with sudo: sudo tools/install_root.sh"; exit 2; }
[ -n "${SUDO_USER:-}" ] && [ "$SUDO_USER" != root ] || { echo "install_root: run it with sudo from your own account, so the checks run as you"; exit 2; }
for f in c/out/release/shale c/out/release/shale-gate tools/shale-gate-root.sh; do
    if [ ! -f "$f" ] || [ -L "$f" ]; then
        echo "install_root: $f is not a file: run \`shale c/install.shale dry=1\` first"; exit 2
    fi
done
head="$(sudo -u "$SUDO_USER" git rev-parse --short HEAD)"
dirty="$(sudo -u "$SUDO_USER" git status --porcelain)"
[ -z "$dirty" ] || { echo "install_root: the checkout has changes: a root install ships a commit"; exit 2; }

install -d -o root -g root -m 755 "$LIB" "$LIB/bin"
# put SRC DEST MODE: SRC copied to DEST.new, root's, then renamed over DEST.
put() {
    install -o root -g root -m "$3" "$1" "$2.new"
    mv -T -f "$2.new" "$2"
}
put c/out/release/shale "$LIB/bin/shale" 755
put c/out/release/shale-gate "$LIB/bin/shale-gate" 755
put tools/shale-gate-root.sh "$LIB/shale-gate.sh" 755
ln -s -f -n "$LIB/bin/shale" /usr/local/bin/shale.new
mv -T -f /usr/local/bin/shale.new /usr/local/bin/shale

# The checks, as the user: the runtime's mode, and the gate through the
# wrapper on a launch (passed) and `ls` (refused).
v="$(sudo -u "$SUDO_USER" "$LIB/bin/shale" --version)"
[ "$v" = "shale ReleaseSafe" ] || { echo "install_root: $LIB/bin/shale --version said: $v"; exit 1; }
echo '{"tool_name":"Bash","tool_input":{"command":"shale a.shale"}}' | sudo -u "$SUDO_USER" "$LIB/shale-gate.sh" \
    || { echo "install_root: the gate refused a launch"; exit 1; }
if echo '{"tool_name":"Bash","tool_input":{"command":"ls"}}' | sudo -u "$SUDO_USER" "$LIB/shale-gate.sh" 2>/dev/null; then
    echo "install_root: the gate passed \`ls\`"; exit 1
fi
echo "installed Shale $head, root's:"
sha256sum "$LIB/bin/shale" "$LIB/bin/shale-gate" "$LIB/shale-gate.sh"
ls -l /usr/local/bin/shale
