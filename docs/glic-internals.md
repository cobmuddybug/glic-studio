# GLIC internals (glic-cpp @ 99c556cf)

Written from a full read of `third_party/glic-cpp-upstream/src/` (design doc §0.3).
Line references are to that pristine copy. Where the original Java (GlitchCodec/GLIC)
behaves differently, that is called out.

## 1. Source map

| File | Responsibility |
|---|---|
| `config.hpp` | Enums (colour spaces, predictions, wavelets, encodings, clamp, transform), `ChannelConfig`, `CodecConfig`, ARGB `Color` helpers |
| `glic.cpp` | The whole pipeline: `encodeToBuffer`, `decodeFromBuffer`, file framing, image load/save (stb) |
| `colorspaces.cpp` | 16 RGB↔X conversions, plus the name↔enum tables for every enum |
| `planes.cpp` | `Planes`: three `int` channel planes stored `[c][x][y]`, power-of-two padded size, out-of-bounds reads return the border colour, and clamp helpers |
| `segment.cpp` | Quadtree segmentation (encode) and reading the split-bit stream (decode) |
| `prediction.cpp` | 24 predictors, plus the SAD/BSAD/RANDOM meta-predictors |
| `quantization.cpp` | Divide/multiply by `q/2` with rounding |
| `wavelet.cpp` | 6 wavelet filter sets, FWT and WPT 2-D transforms, magnitude compressor |
| `encoding.cpp` | 6 bit-packing schemes for the per-pixel values |
| `bitio.cpp` | MSB-first `BitWriter` / `BitReader` |
| `effects.cpp` | 6 post effects applied after decode |
| `main.cpp` | CLI argument parsing |

## 2. Parameters

### Per image
| Param | CLI | Default | Stored in file | Used by decoder |
|---|---|---|---|---|
| colour space | `--colorspace` | HWB | yes (header byte 14) | yes |
| border colour RGB | `--border` | 128,128,128 | yes (bytes 15–17) | yes: the value of out-of-bounds pixels, which predictors read at block edges |

### Per channel (`ChannelConfig`). The CLI sets all three channels to the same value.
| Param | CLI | Default | Range in practice | Stored | Decoder uses |
|---|---|---|---|---|---|
| `minBlockSize` | `--min-block` | 2 | ≥1 (clamped) | no | no (the segmentation itself is stored) |
| `maxBlockSize` | `--max-block` | 256 | ≤512 (clamped) | no | no |
| `segmentationPrecision` (threshold) | `--threshold` | 15 | float ≥0 | no | no |
| `predictionMethod` | `--prediction` | PAETH | enum −3..23 | yes (int8) | yes, as a fallback for every segment whose stored type is NONE (which is every segment for fixed predictors) |
| `quantizationValue` | `--quantization` | 110 | 0–255 (stored as uint8, so it wraps) | yes | yes; the divisor is `q/2`, and `q/2 ≤ 1` means quantization is off |
| `clampMethod` | `--clamp` | none | none, mod256 | yes | yes |
| `waveletType` | `--wavelet` | SYMLET8 | enum 0..40 | yes | yes |
| `transformType` | `--transform` | fwt | fwt, wpt | yes | yes |
| `transformScale` | `--scale` | 20 | must be ≥1 (see §7) | yes (int32) | yes; it also sets the packed bit width |
| `transformCompress` | none | 0 | 0–255 | **no** | no (encode only; not reachable from the CLI) |
| `encodingMethod` | `--encoding` | packed | enum 0..5 | yes | yes |

**What this means for glic-studio's encode/decode split:** a "decode setting" is an
override of a value the header stores: colour space, border, and per channel prediction,
quantization, clamp, wavelet, transform, scale and encoding. Segmentation parameters
(min/max block, threshold) and `transformCompress` exist only at encode time, so the Decode
tab must not show them. "Linked" means "use what the header says".

## 3. Encode pipeline (`GlicCodec::encodeToBuffer`, glic.cpp:35)

```
pixels (ARGB) ─► colour-space convert ─► Planes[3] (int, [x][y], padded to 2^n)
for channel p in 0,1,2 (sequential; shared RNG state crosses channels):
   1. segmentation      quadtree over the padded square ─► segments[p] + split bits
   2. for each segment, in quadtree (Z) order:
        a. predict           pred = predict(method, planes, seg)  (reads already-processed neighbours)
        b. residual          planes -= pred           (clampIn)
        c. quantize          planes = round(planes / (q/2))
        d. wavelet fwd       block/255 ─► FWT/WPT ─► [compress] ─► round(coef*scale/size)
        e. store             resultPlanes[seg] = planes[seg]     ◄── the values that get encoded
        f. wavelet inverse   (coef*size/scale) ─► reverse ─► *255 ─► clamp()
        g. dequantize        planes *= q/2
        h. add prediction    planes += predict(seg.predType, …)  (clampOut)  ◄── see defect D3
   3. prediction records  8 bytes per segment
   4. data encoding       resultPlanes[p] over segments ─► bitstream (packed/rle/…)
assemble the file (§5)
```

Steps f–h rebuild the block "as the decoder will see it", so later blocks predict from
reconstructed neighbours. This closed loop is why GLIC artefacts spread spatially. Defect D3
breaks the loop for fixed predictors.

### 3.1 Segmentation (segment.cpp)
- The root is a square of side `max(paddedW, paddedH)` at (0,0). Recursion visits children in the order TL, TR, BL, BR.
- A node whose origin is outside the *unpadded* image is skipped entirely (no bit, no segment).
- Split if `size > maxSize || (size > minSize && stddev > threshold)`. Write a `1` and recurse on a split, otherwise write a `0` and emit a leaf.
- `stddev` is **estimated from random samples**: `max(0.1·size², 4)` pixel positions drawn with a function-static `std::mt19937(42)`. The draws continue across channels and across calls (defect D2).
- Leaves partly outside the image are normal. Out-of-bounds reads return the border colour, and writes are dropped.
- Decoder: reads the same bits, but **never splits below size 2** (`decision && size > 2`).

### 3.2 Prediction (prediction.cpp)
Every predictor returns a `size×size` int block computed from pixels left of and above the
block (`x−1`, `y−1`, sometimes `x−2`). Out-of-image positions read the border colour.

| Group | Methods | Stateful? |
|---|---|---|
| Original GLIC | NONE(0) CORNER H V DC DCMEDIAN MEDIAN AVG TRUEMOTION PAETH LDIAG HV JPEGLS DIFF REF ANGLE | REF searches 45 random offsets (static `mt19937(12345)`) and records `refX/refY`. ANGLE searches 3 directions × up to 16 angles and records `angle/refAngle` |
| C++ additions | SPIRAL NOISE GRADIENT MIRROR WAVE CHECKERBOARD RADIAL EDGE | no |
| Meta | SAD(−1): the predictor with minimum SAD. BSAD(−2): maximum SAD. RANDOM(−3): uniform over 0..23 | SAD/BSAD set `seg.predType`. RANDOM does **not** record its choice |

Only REF, ANGLE, SAD and BSAD write `seg.predType`. Every other path leaves it at NONE.

### 3.3 Quantization
`quantValue(q) = q/2`. Forward is `round(v / (q/2))` and inverse is `round(v * (q/2))`. Skipped when `q/2 ≤ 1`.

### 3.4 Wavelets (wavelet.cpp)
- Only **6 filter sets** exist: Haar, DB2, DB4, Sym4, Sym8, Coif2. The 41 enum names alias onto them. DB3→DB4, SYM2/3→Sym4, SYM5–10→Sym8, COIF1–5→Coif2. **DB5–DB10 and every BIORTHOGONAL fall through to Haar.** NONE skips the transform.
- FWT: full-depth dyadic decomposition, per row then per column, with periodic extension `(2i+j) mod n`. WPT: a full packet tree to `log2(size)` levels.
- Each segment is transformed **independently** as its own `size×size` block. Coefficients are stored as `round(coef · scale / size)`. So the "wavelet band" layout is per block, with LL in each block's top-left (index order `[x][y]`).
- `MagnitudeCompressor` zeroes |coef| below `50·(c/255)²`. It is unused unless `transformCompress > 0`.

### 3.5 Data encoding (encoding.cpp)
Values are iterated segment by segment, `x` outer and `y` inner. `bits = ceil(log2(scale))`.

| Method | Per value | Notes |
|---|---|---|
| RAW | 32 bits | Ignores every config field |
| PACKED | wavelet≠NONE: `bits+1` unsigned. wavelet=NONE: 9 bits unsigned (clamp none) or 8 bits signed (mod256) | Negative values are truncated to the low bits and **not sign-extended on read** (defect D7) |
| RLE | flag bit, then a 7-bit run length (2..129) if flagged, then a value in PACKED width | A run spans segment boundaries |
| DELTA | zigzag(v − prev) in `bits+2` bits | |
| XOR | (v ^ prev) in PACKED width | |
| ZIGZAG | zigzag(v) in `bits+1` bits | |

Each channel's data stream is byte-aligned at the end.

### 3.6 Prediction record (8 bytes per segment, per channel)
`predType:u8 | refX:i16 | refY:i16 | refAngle%3:u8 | angle·0x7000:i16`, big-endian bit order.
Unset fields are `refX = refY = 32767`, `refAngle = −1` (written as 255, read back as 0), and
`angle = −1.0`.

## 4. Decode pipeline (`decodeFromBuffer`, glic.cpp:300)
1. Check `size ≥ 160` and the magic. Read width, height, colour space, border and the 9 section sizes. **No range checks** (defect D8).
2. Read the 3 channel headers into `ChannelConfig` (prediction, quantization, clamp, wavelet, transform, scale, encoding).
3. Build `Planes` filled with the border colour. Compute padding with an integer loop. This agrees with the encoder's float `log/ceil` for every width 1..65536 (verified).
4. Per channel: read the segmentation, then the prediction records. A NONE record becomes the header's `predictionMethod`, and read errors stop early.
5. Per channel: decode the data stream into the planes. Running out of data stops early and leaves the remaining pixels at the border colour.
6. Per channel, per segment: inverse wavelet, then dequantize, then add `predict(seg.predType)`.
7. `toPixels`: clamp to 0..255, convert back from the colour space. **Alpha is always 255.**
8. Optional post effects (§6).

Decode does not use the CLI's settings at all. `glic decode` always uses the header.

## 5. `.glic` file layout (all integers big-endian)

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | magic `GLC2` (0x474C4332) |
| 4 | 2 | version = 1 (not checked on read) |
| 6 | 4 | width |
| 10 | 4 | height |
| 14 | 1 | colour space enum |
| 15 | 3 | border R, G, B |
| 18 | 12 | segmentation byte sizes, channels 0–2 (u32 ×3) |
| 30 | 12 | prediction byte sizes ×3 |
| 42 | 12 | data byte sizes ×3 |
| 54 | 10 | zero padding |
| 64 | 32×3 | channel headers: `pred:i8, quant:u8, clamp:u8, wavelet:u8, transform:u8, scale:i32, encoding:u8`, then 22 zero bytes |
| 160 | … | seg₀ seg₁ seg₂, then pred₀ pred₁ pred₂, then data₀ data₁ data₂ |

Section sizes: segmentation is about 1 bit per quadtree node. Prediction is 8 bytes × segments.
Data is almost everything. For corruption (M5), the **payload** is bytes 160..end. Corrupting the
data sections gives "classic" glitching. Corrupting segmentation desynchronises the quadtree, and
everything after the damage goes structurally wrong. Corrupting prediction records changes
predictors per block. Corrupting the header (offsets 0–159) usually makes the file undecodable or
explosive (sizes, dimensions), which is why `protect_header` defaults to on.

## 6. Post effects (effects.cpp)
pixelate(blockSize), scanline(intensity), chromatic(offsetX,Y), dither(intensity, 4×4 Bayer),
posterize(levels), glitch shift(blockSize, `mt19937(seed)`, where seed is always 12345 from the
CLI). They are applied in order after decode. They aren't part of the codec and aren't stored in
the file.

## 7. Upstream defects and quirks

"Preserve" means libglic keeps the behaviour, because output must stay byte-identical.
"Fix" means it was undefined behaviour or a crash, so no valid output depended on it.

| # | Defect | Effect | libglic |
|---|---|---|---|
| D1 | `predRef` builds `uniform_int_distribution(-size, y-size-1)`, which is an empty range for any top-row block (`y == 0`). libstdc++ recurses until the stack overflows | **REF, SAD, BSAD and RANDOM always segfault** on encode. Java's `random(lo,hi)` returns `lo` when `hi ≤ lo` | **Fix** (Java semantics: use `lo` without drawing). These four modes have no upstream ground truth. Their goldens are provisional until approved |
| D2 | Two function-static RNGs (`mt19937(42)` for segmentation sampling, `mt19937(12345)` for REF/RANDOM), never reset | Deterministic only for the first encode in a process. A GUI would get a different segmentation on every render | **Fix**: both are reseeded at the start of every encode and every decode. The first-call output is identical, so the baseline still matches |
| D3 | Fixed predictors never set `seg.predType`, so step 2h calls `predict(NONE)`, which returns zeros | The encoder's reconstruction loop adds no prediction back. The decoder *does* add the header's method. Encoder and decoder drift apart, and that drift is part of glic-cpp's look. Java sets `pred_type` in every predictor and has no drift | **Preserve.** A "Java-faithful" switch would be a new, opt-in output mode (needs approval) |
| D4 | RANDOM's choice isn't recorded. The decoder sees RANDOM in the header and re-rolls with its own RNG | Encode and decode use different predictors per block | Preserve. For overlays, libglic's hooks capture the method actually used |
| D5 | `effectFromName` matches upper case only. The CLI help and README use lower case | `--effect scanline` is silently a no-op | Preserve in the CLI (baseline case `fx_lower_noop`). The GUI uses enums, not names |
| D6 | The encoder allows `minBlock = 1`, but the decoder never splits below 2 | Split-bit stream desync | Preserve (baseline case `minblock1`) |
| D7 | PACKED with wavelet=NONE and clamp=none writes negative residuals into 9 unsigned bits, and reads without sign extension | −3 comes back as 509 | Preserve |
| D8 | Section sizes and dimensions from the header are trusted | Truncated or corrupt files read out of bounds, and huge dimensions allocate wildly | **Fix**: clamp sections to the bytes actually present, and reject dimensions ≤0 or >16384. Valid files are unaffected |
| D9 | `transformScale ≤ 0` gives `log2(0)` and casts NaN/−inf to int (UB). Division by zero on encode | Garbage or a hang | **Fix**: invalid encode settings are rejected. A corrupt header scale fails the decode cleanly |
| D10 | DB5–DB10 and BIORTHOGONAL* silently map to Haar | Confusing names | Preserve. The UI lists only distinct filters and says what they alias to |
| D11 | CMake downloads stb from `master` if it's missing | Unpinned dependency | stb is vendored |
| D12 | Codec code prints progress to stdout | Noise from a library | Removed from libglic. The CLI keeps its own messages |
| D13 | bit-io's `readInt(boolean unsigned, int size)` was ported as `readInt(bool isSigned, …)`, which inverts every flag | Negative packed values (wavelet coefficients, residuals, XOR deltas) decode as large positive numbers. RLE run counts are sign-extended. mod256 values lose their sign handling | Found after the refactor. See §10 |
| D14 | Java's GUI stores `transform_scale = 2^slider` (slider 2–24, default 20 → 1,048,576). glic-cpp's `--scale 20` uses 20 literally | Nearly every wavelet coefficient rounds to 0, so wavelet modes decode to flat grey. This is why the default output is grey | Found after the refactor. See §10 |
| D15 | glic-cpp's own wavelet code (instead of JWave) doesn't invert: forward then inverse ≠ identity for every filter, including Haar on 2×2 | Streaks and smearing even with the other bugs fixed | Found after the refactor. See §10 |

## 8. Data available for overlays (Hooks)

| Stage | Data | Notes for M4 |
|---|---|---|
| after segmentation | `segments[p]`: `{x, y, size}` leaves in Z order. Depth = `log2(root / size)` | Different per channel (unless the channels are similar) |
| per segment, encode | effective prediction method, `refX/refY`, `angle/refAngle` | Uniform for fixed predictors. Varied only for SAD/BSAD/RANDOM. Capture the method actually used, not `seg.predType` |
| after step 2e | `resultPlanes[p]`: the integers that get bit-packed. These are wavelet coefficients (scaled) when a wavelet is set, otherwise quantized residuals | This *is* the wavelet band view: per-block layout, LL top-left in each block |
| decode, after data | decoded value planes (equal to `resultPlanes` when uncorrupted) | Lets the band view show corruption |
| decode, end | reconstructed planes in the working colour space | Optional per-channel view |

None of this costs anything when no hook is attached: capture happens behind a null-pointer check.

## 9. Performance notes
- `Planes` is `vector<vector<vector<int>>>` indexed `[c][x][y]`, and every predictor allocates nested vectors per block. Lots of room to speed up later; behaviour is guarded by the golden tests.
- Segmentation of channel `p+1` depends on the RNG state left by channel `p` (D2). So segmentation must stay sequential and in channel order. Prediction, quantization, wavelets and encoding are independent per channel *after* segmentation. That makes them the natural per-channel parallelism point (design doc §9), as long as the REF/RANDOM RNG (also shared) is handled. RANDOM/REF/SAD would need per-channel RNG streams, and that would change output.
- Decode is independent per channel throughout (except RANDOM's RNG).
- Measured upstream on this machine (Release, 640×425 photo, including process start-up): default settings encode 167 ms and decode 96 ms; ANGLE encodes in 399 ms. The 512 px proxy target of under 100 ms (§9 of the design) will need optimisation work.

## 10. Outcome: glic-cpp is not the base (2026-10-25)

After the byte-identical refactor was finished (all 174 baseline files match), the golden
images showed that upstream's *own* output is wrong. The default settings decode a photo to
near-flat grey. D3, D13, D14 and D15 together mean glic-cpp is not a working GLIC
implementation. It can't be fixed by flipping a switch, because its wavelets are wrong too.

Decision (project owner): build libglic as a direct C++ port of the original Java GLIC, verified
byte-for-byte against the Java codec run headless. The glic-cpp refactor is kept in git
history (checkpoint commit) for reference. The parts that don't depend on the codec carry
over: the API shape, GlicData, Hooks, cancellation, per-call RNG, hardening, CLI and test
tooling.
