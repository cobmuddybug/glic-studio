#!/usr/bin/env bash
# Design §M3: preset -> GUI render == preset -> CLI render, byte for byte.
# Runs the real GUI (needs a session) with --export and compares with `glic render`.
# Usage: tools/check_gui_parity.sh [build-dir] [image]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build/rel}"
image="${2:-$root/tests/images/photo.png}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
presets=("$root"/presets/examples/*.json "$root"/presets/glic/0ddangl3.json "$root"/presets/glic/1211.json \
         "$root"/presets/glic/0rg4n1c-tr33.json)
fail=0
for p in "${presets[@]}"; do
    n="$(basename "$p" .json)"
    "$build/glic-cli/glic" render "$image" "$work/cli.png" --preset "$p" >/dev/null
    timeout 120 "$build/app/glic-studio" --open "$image" --preset "$p" --export "$work/gui.png" 2>/dev/null
    if cmp -s "$work/cli.png" "$work/gui.png"; then echo "same  $n"; else echo "DIFF  $n"; fail=1; fi
done
exit $fail
