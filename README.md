# glic-studio

A native Wayland editor for GLIC (GLitch Image Codec), built around exploring the codec's
internals. **Status: M0 (foundation) done.** That's `libglic` and the `glic` CLI. The GUI starts
in M1. See `docs/design.md`.

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
C++20 compiler (GCC or Clang), Ninja. doctest is fetched by CMake.

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
