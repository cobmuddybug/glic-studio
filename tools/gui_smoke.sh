#!/usr/bin/env bash
# GUI smoke test (needs a running Wayland/X session): opens images in each view
# mode, waits for the full render, and writes screenshots to OUT_DIR.
# Usage: tools/gui_smoke.sh [build-dir] [out-dir]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
app="${1:-$root/build/rel}/app/glic-studio"
out="${2:-$root/build/gui-smoke}"
mkdir -p "$out"
run() {
    local name="$1"; shift
    timeout 60 "$app" "$@" --capture "$out/$name.png" 2>&1 | tail -1
}
run glitched --open "$root/tests/images/photo.png" --spec "colorspace=9 pred=-1 wavelet=67 quant=60"
run split --open "$root/tests/images/photo.png" --spec "wavelet=46 scale=4096" --view split
run difference --open "$root/tests/images/gradient.png" --spec "pred=9,15,-3 wavelet=67,0,46 quant=30,60,0" --view difference
run overlays --open "$root/tests/images/photo.png" --spec "pred=-1 wavelet=67 quant=40" --overlays blocks,predictors
run coefficients --open "$root/tests/images/photo.png" --spec "wavelet=46 scale=4096" --overlays coefficients,blocks,ch=2
run corrupt --open "$root/tests/images/photo.png" --preset "$root/presets/examples/corrupt-band.json" --tab corrupt
run mutate --open "$root/tests/images/photo.png" --actions randomize,randomize,undo,randomize,mutate
run history --open "$root/tests/images/photo.png" --actions randomize,randomize,undo,randomize,mutate,adopt:5,mutate
run tiny --open "$root/tests/images/tiny.png" --spec "pred=14 quant=40"
echo "screenshots in $out"
