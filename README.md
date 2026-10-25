# glic-studio

A native Wayland editor for GLIC (the GLitch Image Codec), built around exploring the codec's
internals instead of hiding them. It gives you a live preview, separate encode and decode
settings (so mismatched decoding is a first-class feature), overlays that show the codec's
quadtree, predictors and wavelet bands, a corruption lab, randomize and mutate with a branching
history, and animation export. Everything is deterministic: a render can always be reproduced
exactly from its preset, and exported PNGs carry their preset inside them.

`libglic` is a C++ port of the original [GLIC](https://github.com/GlitchCodec/GLIC)
(Java/Processing). It is byte-identical to GLIC on 220 golden outputs, which are produced by
running the original headless (see `docs/glic-internals.md`). `docs/design.md` is the design
document, with a section per milestone recording where the build departed from the plan.

## Build

Requirements: CMake ≥ 3.20, Ninja, a C++20 compiler (GCC or Clang), and for the GUI SDL3
(≥ 3.2) and OpenGL 3.3. nlohmann/json is used from the system if present. It, Dear ImGui and
doctest are otherwise fetched by CMake.

```sh
# Arch:    pacman -S cmake ninja sdl3 nlohmann-json
# Fedora:  dnf install cmake ninja-build SDL3-devel json-devel mesa-libGL-devel
cmake -S . -B build/rel -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/rel
ctest --test-dir build/rel
```

`-DGLIC_BUILD_APP=OFF` builds only the library and CLI. `-DGLIC_SANITIZE=ON` builds with ASan
and UBSan.

### Install

```sh
cmake --install build/rel --prefix ~/.local     # or /usr/local
```

This installs `glic-studio`, `glic`, the bundled presets, `glic-studio.desktop` and the icon.
The Wayland app ID is `glic-studio`, matching the desktop file, for compositor window rules.
MP4 and GIF export need `ffmpeg` on PATH. Without it, animations export as PNG frames.

## The app

```sh
glic-studio [image]          # or drop an image / preset onto the window
```

The canvas is on the left, settings on the right (below the canvas in narrow windows), and the
history strip is under the canvas. Each change shows a fast preview first (512 px longest edge,
adjustable under Preview). The full-resolution render follows once input has been quiet for
150 ms. Decode-only changes reuse the encoded data, and so do overlays and corruption.

| Tab | What it does |
|---|---|
| **Encode** | Colour space, border, seed, and per channel: block sizes, threshold, prediction, quantization, clamp, wavelet (67 JWave filters), transform, compression, scale, encoding. Right-click a control to reset it or copy it to all channels. |
| **Decode** | "Linked to encode" decodes with what the file says. Unlinked, the decoder uses these values instead (GLIC's skip-header mode), giving mismatched decodes. A "mismatch" badge shows in the status bar. |
| **Corrupt** | Replaces random bytes of the encoded file before decoding, with a seed, a rate and optional header protection. The byte map shows the file's sections and where bytes changed. Drag across it to add a region with its own rate; right-click a region to remove it. A file that fails to decode keeps the last good render on screen. |
| **Animate** | Sweeps one parameter (or the corruption seed) from a start to an end value over N frames, with easing, and exports MP4, GIF or PNG frames. Each frame PNG embeds its own preset. A preview slider renders any frame without touching your settings. |

**Overlays** (`O`) show the codec's internals for one channel: block outlines coloured by
quadtree depth, a heatmap of the predictor each block was decoded with (with a legend), and the
decoded wavelet coefficients. Point at the image to read the block, predictor and value
under the cursor.

**Exploration.** `R` randomizes within ranges that stay usable. `M` opens the mutate grid: nine
variations of the current settings at a chosen strength. Click one to adopt it, and the grid
regenerates around it. Every settled change becomes a node in the branching history. Undo and
redo follow the current branch, and a change after an undo starts a new branch without losing
the old one. Click a thumbnail to jump to it; right-click one with a branch count to switch
branch.

| Key | Action |
|---|---|
| Ctrl+O | Open an image or preset (portal dialog; drag-and-drop works too) |
| Ctrl+S | Export PNG (full resolution, preset embedded) |
| Ctrl+Shift+S | Save preset JSON |
| Ctrl+C | Copy the render to the clipboard |
| Ctrl+Z / Ctrl+Shift+Z (or Ctrl+Y) | Undo / redo along the current branch |
| R | Randomize |
| M | Mutate grid (Esc or M closes it) |
| L | Toggle decode linked / unlinked |
| C | Toggle corruption |
| N | New corruption seed |
| O | Codec overlays |
| Tab | Cycle view: glitched, original, split, difference |
| Space (hold) | Show the original; Space+drag pans |
| wheel, `+` / `-`, `0`, `1` | Zoom, fit, 100%; middle-drag pans |

**Presets.** The Presets menu has hand-made examples and GLIC's own 144 presets, converted.
Dropping a PNG exported by glic-studio asks whether to load its settings or use it as the
source image.

**Settings persistence.** Recent files, the last settings, the window size and the dock layout
are kept in `$XDG_CONFIG_HOME/glic-studio/` (normally `~/.config/glic-studio/`). Settings given
on the command line replace the last session's. `--no-config` ignores the directory, and
`GLIC_STUDIO_CONFIG_DIR` points it somewhere else.

## The CLI

```sh
glic render photo.png out.png --preset spiral.json   # exactly the GUI's output, corruption included
glic render photo.png out.png --preset exported.png  # the preset embedded in a glic-studio PNG
glic encode photo.png out.glic --spec "colorspace=9 pred=-1 wavelet=67 quant=40" --seed 7
glic decode out.glic out.png                          # as the file says
glic decode out.glic odd.png --override "pred=4"      # mismatched decode
glic --help                                           # every key and GLIC id
```

`encode` writes clean `.glic` data. Corruption is applied by `render`, between encode and
decode, as the GUI does it.

## Preset format

```json
{
  "format": "glic-studio-preset", "version": 1, "name": "corrupt band",
  "encode": { "colorspace": "YCbCr", "border": [128, 128, 128], "seed": 0,
              "channels": [ { "min_block": 4, "max_block": 128, "threshold": 20,
                              "prediction": "PAETH", "quantization": 30, "clamp": "none",
                              "wavelet": "Haar", "transform": "FWT", "compression": 0,
                              "scale": 65536, "encoding": "PACKED" } ] },
  "decode": { "linked": true },
  "corruption": { "enabled": true, "seed": 1337, "mode": "random_bytes", "rate": 0.0002,
                  "protect_header": true,
                  "regions": [ { "start": 0.62, "end": 0.66, "rate": 0.002 } ] }
}
```

Values can be names or GLIC ids. A channel list with one entry applies to all three. Region
positions are fractions of the payload, so a preset works at any image size. Unknown fields are
kept when a preset is saved again.

## Testing

```sh
ctest --test-dir build/rel                       # golden, unit, CLI, and the corruption fuzz
tools/gui_smoke.sh                               # GUI screenshots of every view and feature
tools/check_gui_parity.sh                        # GUI export == `glic render`, byte for byte
```

- **Golden tests** compare libglic with the original GLIC: 219 encodes byte for byte and 219
  decodes pixel for pixel.
- **The fuzz** (`corruption_fuzz`) decodes 1,000 corrupted files at rates up to 0.5. Run it
  under the sanitizer build too.
- **The GUI scripts** need a running session. `glic-studio --actions …`, `--overlays …` and
  `--capture` drive the GUI headlessly; see `glic-studio --help`.

To regenerate the ground truth, run `tools/make_baseline.sh`. It downloads Processing 3.5.4
into `build/` and runs GLIC from `third_party/GLIC` headless over `tests/baseline/cases.tsv`.
Only do this when the cases or test images change.

## Licences

glic-studio is MIT (see `LICENSE`). It ports GLIC (MIT) and JWave (MIT), uses stb (public
domain/MIT), SDL3 (zlib), Dear ImGui (MIT) and nlohmann/json (MIT), and runs ffmpeg as a
separate program when it is available. `NOTICE.md` has the details, including the bundled
presets and the test photo (public domain, U.S. National Park Service).
