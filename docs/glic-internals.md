# GLIC internals

libglic is a direct C++ port of the original GLIC (GlitchCodec/GLIC, Java/Processing).
This document covers the codec as the port implements it, the exact numeric semantics it has
to reproduce, and how the ground truth is produced. Section 11 records why glic-cpp (the C++
port the design doc first named) was evaluated and rejected.

## 1. Provenance

| Piece | Version | Role |
|---|---|---|
| GLIC | `GlitchCodec/GLIC` @ `460e61bf` (vendored in `third_party/GLIC`) | The codec: `codec.pde`, `planes.pde`, `segmentation.pde`, `predictions.pde`, `quantization.pde`, `transformation.pde`, `encoding.pde`, `colorspaces.pde` |
| JWave | `JWave.jar` bundled with GLIC, built 2016-01-09. Matches JWave git `3afd122`, and was verified bit-for-bit against the jar | The wavelet filters and transforms (FWT, WPT) |
| bit-io | `bit-io-1.3.7.jar` bundled with GLIC (jinahya/bit-io tag `1.3.7`) | The bit packing for segmentation and data. Reimplemented from its behaviour, not copied |
| Processing | 3.5.4 (Java 8u202), used only to run the ground-truth harness | Defines the semantics: the preprocessor turns every decimal literal into a float literal, and `random()`, `round()`, `log()` etc. are Processing's |

## 2. Source map

| GLIC | libglic |
|---|---|
| `codec.pde`: `encode()`, `decode()`, `GlicCodecWriter`, `GlicCodecReader` | `src/codec.cpp` |
| `planes.pde`: `Planes`, `RefColor`, clamps | `src/planes.{hpp,cpp}` |
| `segmentation.pde`, `predictions.pde` | `src/predict.{hpp,cpp}` |
| `quantization.pde` | `src/codec.cpp` (`quantize`) |
| `transformation.pde` + JWave | `src/wavelets.{hpp,cpp}`, `src/wavelet_tables.inc` (generated from the jar by `tools/gen_wavelet_tables.py`) |
| `colorspaces.pde` | `src/colorspaces.{hpp,cpp}` |
| bit-io `DefaultBitOutput`/`DefaultBitInput` | `src/bitio.{hpp,cpp}` |
| Processing/Java runtime semantics | `src/jvm.hpp` |

## 3. Parameters

Ids are GLIC's own. They are written into `.glic` files and presets.

### Per image
| Param | Ids / range | GUI default | Stored |
|---|---|---|---|
| colour space | 0 OHTA, 1 RGB, 2 CMY, 3 HSB, 4 XYZ, 5 YXY, 6 HCL, 7 LUV, 8 LAB, 9 HWB, 10 R-GGB-G, 11 YPbPr, 12 YCbCr, 13 YDbDr, 14 Greyscale, 15 YUV | RGB | byte |
| border ("colour outside") | RGB | 128,128,128 | 3 bytes |

### Per channel
| Param | Range | GUI control → value | GUI default | Stored | Used by decoder |
|---|---|---|---|---|---|
| min block | power of two | slider 1–9 → `1<<v` | 4 | no | no |
| max block | power of two, ≤512 effective | slider 1–9 → `1<<v` | 256 | no | no |
| threshold | float | slider 5–250 | 15 | no | no |
| prediction | −3 RANDOM, −2 BSAD, −1 SAD, 0 NONE … 15 ANGLE | list | AVG (7) | byte; meta predictors are stored as NONE, with per-block choices in the prediction section | yes |
| quantization | 0–255 (the divisor is `q/2`, off when `q/2 ≤ 1`) | slider | 0 | byte (wraps; encode uses the full int) | yes |
| clamp | 0 none, 1 mod256 | radio | none | byte | yes |
| wavelet | 0 none, 1–67 (JWave), −1 random | list | none | byte (random is resolved first) | yes |
| transform type | 0 FWT, 1 WPT, −1 random | radio | FWT | byte (resolved) | yes |
| compression | 0–255 | slider | 0 | no | no |
| scale | int | slider 2–24 → `2^v` | 2²⁰ | int32 | yes; it also sets the packed bit width `ceil(log2(scale))+1` |
| encoding | 0 raw, 1 packed, 2 rle | list | packed | byte | yes |

`EncodeSettings` defaults to the GUI defaults. GLIC's own `CodecConfig` literals (HWB, raw,
PAETH, q110, Symlet 9, **scale 20**) are available as `glicCodecConfigDefaults()`. With
scale 20 nearly every wavelet coefficient rounds to zero and images decode to grey. The GUI
never uses that value; glic-cpp copied it.

## 4. Encode (`encode()` in codec.pde)

```
seed RNG
header 1: "GLIC", w, h, colour space, border; pad to 128
resolve random wavelet / transform type per channel   (RNG)
header 2: 3 × [CHn mark, pred(meta→0), quant, clamp, wavelet, ttype, scale, enc]; pad each to 32
planes = to colour space (border colour converted too)
"SEGMENTATION " + per channel: mark + quadtree split bits      (RNG: stddev sampling)
512 × 0xFF
for each channel:
  wavelet/transform objects (unknown ids → RNG pick)
  for each block (quadtree Z order):
    pred = predict(cfg)          (RNG for RANDOM/REF; SAD/BSAD try 0..15)
    planes -= pred               (clampIn)
    quantize ÷ q/2
    forward 2-D wavelet (block/255.0f) [magnitude compress] → round(c·scale/size)
    result[block] = planes[block]                 ◄ the values that get encoded
    inverse: (size·v)/(float)scale → reverse → round(x·255) clamped
    dequantize × q/2
    planes += predict(block.pred_type)            (clampOut)
"PREDICTDATA " + per channel: mark + 8 bytes per block
512 × 0xFF
"IMAGEDATA " + per channel: mark + raw | packed | rle of result
patch section sizes at offset 16
```

Predictors read only neighbours to the left of and above the block (REF and ANGLE also read
further back). Positions outside the image read the border colour. The encoder's
reconstruction loop is closed: later blocks predict from reconstructed data.

## 5. Decode (`decode()` / `GlicCodecReader`)

The reader is a `DataInputStream` walk with fixed skips. libglic emulates it over the byte
buffer:

- `w = max(64, |stored| % 8192)`, and the same for `h`. Images smaller than 64 px decode at 64.
- Section sizes are masked (`& 0x7ffff`, `& 0xffffff`, `& 0x3ffffff`). `readArray(n)` returns
  `n` bytes, zero-filled past the end of the file. Raw data is read straight from the stream.
- The segmentation reader never splits below 2 (so `min=1` desynchronises, as in GLIC).
- Prediction records: a stored NONE means "use the header's method". A partial record keeps
  partial values.
- End-of-data inside a section stops that section quietly. End-of-file inside the 224-byte
  headers fails the decode. An invalid packed bit width (scale ≤ 0 with a wavelet) throws, and
  the decode fails.
- `toPixels` ORs the channels **unclamped**. Values outside 0–255 bleed into neighbouring bytes,
  which is part of GLIC's look. Alpha is 0xFF (GLIC takes it from the loaded source image; the
  harness decodes with none loaded).

**Skip-header mode** (GLIC's `do_skip_header`) is the mismatch feature: the decoder takes colour
space, border and every per-channel decode value from the settings instead of the header.
Meta predictors become NONE (`max(0, pred)`), and a random wavelet or transform picks at decode
time. `DecodeSettings::override` is exactly this.

## 6. File layout (big-endian)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `GLIC` |
| 4 | 4 | width |
| 8 | 4 | height |
| 12 | 1 | colour space |
| 13 | 3 | border RGB |
| 16 | 16 | segmentation sizes ×4 (4th unused) |
| 32 | 16 | prediction sizes ×4 |
| 48 | 16 | data sizes ×4 |
| 64 | 64 | zero |
| 128 | 3×32 | `CH0n`, pred, quant, clamp, wavelet, ttype, scale:i32, enc, 18 zero bytes |
| 224 | 13 | `SEGMENTATION ` |
| … | | per channel: `CH0n` + split bits |
| | 512 | 0xFF |
| | 12 | `PREDICTDATA ` |
| | | per channel: `CH0n` + per block `pred:u8 refx:i16 refy:i16 refa:u8 (int)(0x7000·angle):i16` |
| | 512 | 0xFF |
| | 10 | `IMAGEDATA ` |
| | | per channel: `CH0n` + data |

`GlicData::layout()` reports every section's offset and declared size as the reader sees them.
That is what the corruption lab's byte map and header protection (offsets 0–223) are built on.

## 7. Numeric semantics the port reproduces

| Java/Processing | Port |
|---|---|
| Decimal literals are floats after preprocessing (`255.0` → `255.0f`) | Ported from the preprocessed source. `get()/255.0f` is a float division widened to double; getting this wrong caused the only mismatch found during the port |
| float arithmetic in float, no FMA | `-ffp-contract=off`, no fast-math, SSE |
| int overflow wraps | `-fwrapv` |
| `(int)` of float/double saturates, NaN → 0 | `jvm::toInt` |
| `Math.round(float)` = exact `floor(x+0.5)`, saturating (verified on 8u202) | `jvm::round` |
| `log`, `sqrt`, `pow`, `ceil`, `floor` = `(float)Math.f((double)x)` | `jvm::` helpers |
| `map`, `constrain`, `min/max(a,b,c)`, `sq`, `abs` | `jvm::` helpers |
| `color(r,g,b)` clamps and sets alpha 0xFF | `codec.cpp` |
| `java.util.Random` + Processing `random(high)` / `random(lo,hi)` (with the re-draw loop and `lo >= hi → lo`) | `jvm::JavaRandom`, `jvm::ProcessingRandom` |
| bit-io: size limits (31 bits unsigned, 32 signed), MSB-first, low bits written, sign-extend on signed read, `IllegalStateException` past the end | `bitio.cpp` |
| JWave `getExponent`/`isBinary` via `Math.log`/`Math.pow` | `wavelets.cpp` |

## 8. Randomness and determinism

Everything random draws from one `ProcessingRandom`, seeded from `EncodeSettings::seed` (or
`DecodeSettings::seed`) at the start of each call. The draws happen in GLIC's order:

1. Resolving random wavelet/transform type, per channel.
2. Segmentation: the std-dev estimate samples `max(0.1·size², 4)` random pixel pairs per quadtree node, across all three channels in order.
3. Per block: RANDOM picks a predictor. REF searches 45 random offsets (also inside SAD/BSAD, which try REF).
4. Unknown wavelet or transform ids (from settings or a corrupt header) pick at random.

GLIC itself never seeds, so its output varies between runs. The harness seeds it with the same
values, which makes the comparison exact. Same image + settings + seed always gives the same
bytes (tested in-process).

## 9. Data available for overlays (`Hooks`)

| Stage | Data |
|---|---|
| after segmentation | blocks `{x, y, size}` per channel in Z order. Depth = `log2(rootSize/size)` |
| per block | the predictor actually applied (RANDOM and SAD/BSAD resolved) |
| encode `result` planes | the integers that get packed: scaled wavelet coefficients (per-block layout, LL at the top-left of each block) or quantized residuals |
| decode, after data | the decoded values (equal to the encoded ones when uncorrupted). This makes corruption visible in the band view |

Capture is behind null checks and costs nothing when off.

## 10. Ground truth

`tools/java-harness/` assembles a sketch from GLIC's codec tabs (the GUI and main tabs are
replaced by a stub), runs it through Processing 3.5.4's preprocessor and compiler, and drives it
from `HarnessMain`. `tools/make_baseline.sh` runs `tests/baseline/cases.tsv`: 134 cases, 220
image/case outputs. They cover every colour space, every predictor, all 67 wavelets, WPT, all
encodings, clamp, compression, low and invalid scales, per-channel settings, borders, sub-64 px
images, nine mismatch decodes and an expected encode failure. The results are stored as
`.glic` (in `glic.tar.xz`), decoded PNGs and failure markers. The harness also dumps the JWave
filter tables (as hex doubles) and has probes for transforms and `random()`.

`tests/test_golden.cpp` runs every case in a single process. libglic matches all 219 encodes
byte for byte and all 219 decodes pixel for pixel, and fails the one case GLIC fails.

## 11. Appendix: glic-cpp (evaluated and rejected)

The design doc first named `daitomanabe/glic-cpp` as the base. It was refactored into a library
that matched upstream byte for byte (commit `735d39e`). The golden images then showed that
upstream's output is wrong:

- It segfaults on REF, SAD, BSAD and RANDOM (`uniform_int_distribution` with an empty range on top-row blocks).
- bit-io's `readInt(boolean unsigned, …)` was ported as `isSigned`, so every sign is inverted on read.
- Scale 20 is taken literally (GLIC's GUI uses 2^20), so wavelet modes decode to grey.
- Its hand-written wavelets don't invert. Forward then reverse isn't the identity, even for Haar.
- Fixed predictors never record their type, so the encoder's reconstruction loop adds back zero.
- Static RNGs are never reseeded, and only 6 filters stand behind its 41 wavelet names.

glic-cpp also adds 8 predictors, 3 encodings and 6 post effects that aren't in GLIC. They're
MIT and could return later as clearly labelled extras.
