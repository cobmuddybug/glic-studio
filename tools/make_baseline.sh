#!/usr/bin/env bash
# Regenerate tests/baseline/ from the ORIGINAL GLIC via the headless Java harness.
#
# This is ground truth for libglic. Rerun only when cases.tsv or the test images
# change; libglic itself never writes here.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

[[ -x build/java-harness/run.sh ]] || tools/java-harness/build.sh

out=tests/baseline
find "$out" -mindepth 1 -maxdepth 1 -type d -exec rm -rf {} +
rm -f "$out/glic.tar.xz" "$out/SHA256SUMS"

build/java-harness/run.sh batch "$out/cases.tsv" tests/images "$out"

cd "$out"
find . -name '*.glic' -o -name '*.png' -o -name '*-fails' | sort | xargs sha256sum > SHA256SUMS
# .glic files are large but compress very well; keep only a reproducible tarball.
find . -name '*.glic' | sort | tar --sort=name --mtime='2026-01-01' --owner=0 --group=0 \
    --numeric-owner -cf - -T - | xz -9 -T1 > glic.tar.xz
find . -name '*.glic' -delete
echo "baseline: $(grep -c . SHA256SUMS) files, $(du -sh . | cut -f1)"
