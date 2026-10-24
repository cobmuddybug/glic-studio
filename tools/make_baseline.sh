#!/usr/bin/env bash
# Regenerate tests/baseline/ from UNMODIFIED glic-cpp (third_party/glic-cpp-upstream).
#
# This is ground truth for libglic's golden tests. Only rerun it if the upstream
# copy changes, and never point it at libglic.
#
# Usage: tools/make_baseline.sh [glic-binary]
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

glic="${1:-}"
if [[ -z "$glic" ]]; then
    cmake -S third_party/glic-cpp-upstream -B build/upstream -G Ninja \
        -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build build/upstream >/dev/null
    glic="$root/build/upstream/glic"
fi

out=tests/baseline
images=(tests/images/*.png)

find "$out" -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +

while IFS=$'\t' read -r name imgs enc dec; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    [[ "$enc" == "-" ]] && enc=""
    [[ "$dec" == "-" ]] && dec=""

    for img in "${images[@]}"; do
        stem="$(basename "$img" .png)"
        if [[ "$imgs" != "all" && ",$imgs," != *",$stem,"* ]]; then
            continue
        fi
        mkdir -p "$out/$stem"
        # shellcheck disable=SC2086
        "$glic" encode "$img" "$out/$stem/$name.glic" $enc >/dev/null
        # shellcheck disable=SC2086
        "$glic" decode "$out/$stem/$name.glic" "$out/$stem/$name.png" $dec >/dev/null
    done
done < "$out/cases.tsv"

cd "$out"
find . -name '*.glic' -o -name '*.png' | sort | xargs sha256sum > SHA256SUMS
# .glic files are ~50 MB raw but compress ~140x; keep only a reproducible tarball.
find . -name '*.glic' | sort | tar --sort=name --mtime='2026-01-01' --owner=0 --group=0 \
    --numeric-owner -cf - -T - | xz -9 -T1 > glic.tar.xz
find . -name '*.glic' -delete
echo "baseline: $(grep -c . SHA256SUMS) files"
