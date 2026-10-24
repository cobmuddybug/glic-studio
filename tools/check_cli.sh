#!/usr/bin/env bash
# CLI end-to-end check: encode a sample of baseline cases with the glic binary
# and compare the .glic bytes with the original GLIC's.
# Usage: tools/check_cli.sh GLIC UNPACKED_BASELINE_DIR
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
glic="$1"
glics="$2"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
n=0
for name in guidefault haar_sad bsad_ycbcr_rle perchannel wpt_db4_yuv; do
    line="$(grep -P "^$name\t" "$root/tests/baseline/cases.tsv")"
    seed="$(cut -f3 <<<"$line")"
    spec="$(cut -f4 <<<"$line")"
    for img in small64 photo; do
        "$glic" encode "$root/tests/images/$img.png" "$work/out.glic" --glic-defaults --seed "$seed" --spec "$spec" >/dev/null
        cmp "$work/out.glic" "$glics/$img/$name.glic"
        "$glic" decode "$glics/$img/$name.glic" "$work/out.png" --seed "$seed" >/dev/null
        n=$((n + 1))
    done
done
echo "OK: $n CLI encodes identical to GLIC"
