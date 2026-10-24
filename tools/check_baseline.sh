#!/usr/bin/env bash
# Run a golden case matrix with GLIC and compare every output byte-for-byte.
# Usage: tools/check_baseline.sh GLIC [DIR]   (DIR defaults to tests/baseline)
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
glic="$(realpath "$1")"
dir="$root/${2:-tests/baseline}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/tests"
cp -r "$root/tests/images" "$work/tests/images"
mkdir -p "$work/out"
cp "$dir/cases.tsv" "$work/out/"
GLIC_WORK_ROOT="$work" "$root/tools/make_baseline.sh" "$glic" out >/dev/null

if diff -u "$dir/SHA256SUMS" "$work/out/SHA256SUMS"; then
    echo "OK: $(grep -c . "$dir/SHA256SUMS") files identical"
else
    echo "FAIL: outputs differ from $dir" >&2
    exit 1
fi
