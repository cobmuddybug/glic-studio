# glic-studio

A native Wayland editor for GLIC (GLitch Image Codec), built around exploring the codec's
internals. **Status: M2 (encode/decode split) done.** That's `libglic`, the `glic` CLI, and the GUI with a
live preview, view modes, PNG export and mismatched decoding. See `docs/design.md` for the roadmap.

`libglic` is a C++ port of the original [GLIC](https://github.com/GlitchCodec/GLIC) (Java/Processing).
It is byte-identical to GLIC on 220 golden outputs, which are produced by running the
original headless (see `docs/glic-internals.md`).

## Build

```sh
cmake -S . -B build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/rel
ctest --test-dir build/rel            # golden + unit tests
```

Sanitizer build: add `-DGLIC_SANITIZE=ON` (ASan + UBSan). Requirements: CMake ≥ 3.20, a
C++20 compiler (GCC or Clang), Ninja, and for the GUI SDL3 (≥ 3.2) and OpenGL 3.3. doctest and
Dear ImGui are fetched by CMake. To build the library and CLI only, pass `-DGLIC_BUILD_APP=OFF`.

## GUI

```sh
build/rel/app/glic-studio [image]
```

| Key | Action |
|---|---|
| Ctrl+O | Open image (portal dialog; drag-and-drop works too) |
| Ctrl+S | Export PNG |
| Ctrl+C | Copy the render to the clipboard |
| Tab | Cycle view: glitched, original, split, difference |
| L | Toggle decode linked/unlinked (mismatched decoding, Decode tab) |
| Space (hold) | Show the original; Space+drag pans |
| wheel, `+`/`-`, `0`, `1` | Zoom, fit, 100%; middle-drag pans |

Each change shows a fast preview first (512 px longest edge, adjustable under Preview). The
full-resolution render follows once input has been quiet for 150 ms. Decode-only changes reuse
the cached encoded data and only decode again. The app ID is `glic-studio`,
for compositor window rules. `tools/gui_smoke.sh` opens test images in each view mode and saves
screenshots. It needs a running session.

## CLI

```sh
glic encode photo.png out.glic --spec "colorspace=9 pred=-1 wavelet=67 quant=40" --seed 7
glic decode out.glic out.png                      # as the file says
glic decode out.glic odd.png --override "pred=4"  # mismatched decode
glic --help                                       # all keys and GLIC ids
```

Defaults are GLIC's GUI defaults. `--glic-defaults` starts from GLIC's `CodecConfig` instead.

## Regenerating ground truth

`tools/make_baseline.sh` downloads Processing 3.5.4 into `build/`, runs GLIC from
`third_party/GLIC` headless over `tests/baseline/cases.tsv`, and rewrites `tests/baseline/`.
Only do this when the cases or test images change.

## Licences

MIT. See `LICENSE` and `NOTICE.md` (GLIC: MIT; JWave: MIT; stb: PD/MIT).
