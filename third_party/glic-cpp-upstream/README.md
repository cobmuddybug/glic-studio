# GLIC C++ - GLitch Image Codec

[日本語](#日本語) | [English](#english)

---

## 日本語

C++によるGLIC (GLitch Image Codec) のコマンドライン実装です。

### クレジット / Credits

**このプロジェクトは [GlitchCodec/GLIC](https://github.com/GlitchCodec/GLIC) のJava/Processing版をC++にポートしたものです。**

- **オリジナル**: [GlitchCodec/GLIC](https://github.com/GlitchCodec/GLIC) (Java/Processing)
- **ドキュメント**: [GLIC Documentation](https://docs.google.com/document/d/1cdJvEmSKNAkzkU0dFUa-kb_QJB2ISQg-QfCqpHLFlck/edit) - GlitchCodec/GLICより
- **C++ポート**: このリポジトリ

オリジナルのProcessing版から完全にポートし、さらに新しいグリッチ効果を追加しています。

### 特徴

- Processing版の全機能をC++17で再実装
- クロスプラットフォーム対応 (macOS, Linux, Windows)
- コマンドラインインターフェース
- 24種類の予測アルゴリズム（8種類追加）
- 6種類のエンコーディング方式（3種類追加）
- 6種類のポストプロセッシングエフェクト（新機能）

### ビルド方法

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### 使用方法

```bash
# エンコード（画像 → GLIC形式）
./glic encode input.png output.glic [options]

# デコード（GLIC形式 → 画像）
./glic decode input.glic output.png [options]
```

### エンコードオプション

| オプション | デフォルト | 説明 |
|-----------|----------|------|
| `--colorspace <name>` | HWB | 色空間 |
| `--min-block <size>` | 2 | 最小ブロックサイズ |
| `--max-block <size>` | 256 | 最大ブロックサイズ |
| `--threshold <value>` | 15 | セグメンテーション閾値 |
| `--prediction <method>` | PAETH | 予測方式 |
| `--quantization <value>` | 110 | 量子化値 (0-255) |
| `--clamp <method>` | none | クランプ方式 (none, mod256) |
| `--wavelet <name>` | SYMLET8 | ウェーブレット |
| `--transform <type>` | fwt | 変換タイプ (fwt, wpt) |
| `--scale <value>` | 20 | 変換スケール |
| `--encoding <method>` | packed | エンコード方式 |
| `--border <r,g,b>` | 128,128,128 | 境界色 (RGB) |

### デコードオプション（ポストエフェクト）

| オプション | デフォルト | 説明 |
|-----------|----------|------|
| `--effect <name>` | - | エフェクト適用（複数指定可） |
| `--effect-intensity <n>` | 50 | エフェクト強度 (0-100) |
| `--effect-blocksize <n>` | 8 | ブロックサイズ (pixelate, glitch用) |
| `--effect-offset <x,y>` | 2,0 | 色収差オフセット |
| `--effect-levels <n>` | 4 | ポスタライズレベル数 |

### 使用例

```bash
# 基本的なエンコード・デコード
./glic encode photo.png glitched.glic
./glic decode glitched.glic result.png

# スパイラル予測（渦巻き状のアーティファクト）
./glic encode photo.png out.glic --prediction SPIRAL --quantization 180

# 波形予測 + YUV色空間
./glic encode photo.png out.glic --colorspace YUV --prediction WAVE

# ポストエフェクトを適用
./glic decode out.glic result.png --effect scanline --effect chromatic

# 複数エフェクトの組み合わせ
./glic decode out.glic result.png --effect posterize --effect-levels 4 --effect glitch
```

### 機能一覧

#### 色空間 (16種類)
RGB, HSB, HWB, OHTA, CMY, XYZ, YXY, LAB, LUV, HCL, YUV, YPbPr, YCbCr, YDbDr, GS, R-GGB-G

#### 予測アルゴリズム (24種類)

**基本予測 (16種類 - オリジナルGLIC):** NONE, CORNER, H, V, DC, DCMEDIAN, MEDIAN, AVG, TRUEMOTION, PAETH, LDIAG, HV, JPEGLS, DIFF, REF, ANGLE

**C++版で追加 (8種類):**
| 名前 | 説明 |
|------|------|
| SPIRAL | 中心からスパイラル状に予測 |
| NOISE | 位置ハッシュベースのノイズ |
| GRADIENT | 4コーナーからバイリニア補間 |
| MIRROR | ミラー/反転予測 |
| WAVE | 正弦波ベースの変位 |
| CHECKERBOARD | 市松模様で交互予測 |
| RADIAL | 中心からの放射状グラデーション |
| EDGE | エッジ検出ベースの予測 |

**メタ予測:** SAD, BSAD, RANDOM

#### エンコード方式 (6種類)

**基本 (3種類 - オリジナルGLIC):** raw, packed, rle

**C++版で追加 (3種類):** delta, xor, zigzag

#### ポストエフェクト (6種類) - C++版新機能

| 名前 | 説明 |
|------|------|
| pixelate | ピクセル化（モザイク効果） |
| scanline | スキャンライン（CRTモニター風） |
| chromatic | 色収差（RGBチャンネルオフセット） |
| dither | ディザリング（Bayerパターン） |
| posterize | ポスタライズ（色数削減） |
| glitch | グリッチシフト（ランダムな行ずれ） |

#### ウェーブレット変換
Haar, Daubechies (DB2-DB10), Symlet (SYM2-SYM10), Coiflet (COIF1-COIF5)

### 依存関係

- C++17 以上
- CMake 3.14 以上
- stb_image / stb_image_write (ビルド時に自動ダウンロード)

---

## English

A C++ command-line implementation of GLIC (GLitch Image Codec).

### Credits

**This project is a C++ port of the Java/Processing version of [GlitchCodec/GLIC](https://github.com/GlitchCodec/GLIC).**

- **Original**: [GlitchCodec/GLIC](https://github.com/GlitchCodec/GLIC) (Java/Processing)
- **Documentation**: [GLIC Documentation](https://docs.google.com/document/d/1cdJvEmSKNAkzkU0dFUa-kb_QJB2ISQg-QfCqpHLFlck/edit) - From GlitchCodec/GLIC
- **C++ Port**: This repository

This is a complete port from the original Processing version with additional glitch effects.

### Features

- Full reimplementation of Processing version in C++17
- Cross-platform support (macOS, Linux, Windows)
- Command-line interface
- 24 prediction algorithms (+8 new)
- 6 encoding methods (+3 new)
- 6 post-processing effects (new feature)

### Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

### Usage

```bash
# Encode (image → GLIC format)
./glic encode input.png output.glic [options]

# Decode (GLIC format → image)
./glic decode input.glic output.png [options]
```

### Encode Options

| Option | Default | Description |
|--------|---------|-------------|
| `--colorspace <name>` | HWB | Color space |
| `--min-block <size>` | 2 | Minimum block size |
| `--max-block <size>` | 256 | Maximum block size |
| `--threshold <value>` | 15 | Segmentation threshold |
| `--prediction <method>` | PAETH | Prediction method |
| `--quantization <value>` | 110 | Quantization value (0-255) |
| `--clamp <method>` | none | Clamp method (none, mod256) |
| `--wavelet <name>` | SYMLET8 | Wavelet type |
| `--transform <type>` | fwt | Transform type (fwt, wpt) |
| `--scale <value>` | 20 | Transform scale |
| `--encoding <method>` | packed | Encoding method |
| `--border <r,g,b>` | 128,128,128 | Border color (RGB) |

### Decode Options (Post Effects)

| Option | Default | Description |
|--------|---------|-------------|
| `--effect <name>` | - | Apply effect (can be used multiple times) |
| `--effect-intensity <n>` | 50 | Effect intensity (0-100) |
| `--effect-blocksize <n>` | 8 | Block size (for pixelate, glitch) |
| `--effect-offset <x,y>` | 2,0 | Chromatic aberration offset |
| `--effect-levels <n>` | 4 | Posterize levels |

### Examples

```bash
# Basic encode/decode
./glic encode photo.png glitched.glic
./glic decode glitched.glic result.png

# Spiral prediction (spiral artifacts)
./glic encode photo.png out.glic --prediction SPIRAL --quantization 180

# Wave prediction + YUV color space
./glic encode photo.png out.glic --colorspace YUV --prediction WAVE

# Apply post effects
./glic decode out.glic result.png --effect scanline --effect chromatic

# Combine multiple effects
./glic decode out.glic result.png --effect posterize --effect-levels 4 --effect glitch
```

### Feature List

#### Color Spaces (16 types)
RGB, HSB, HWB, OHTA, CMY, XYZ, YXY, LAB, LUV, HCL, YUV, YPbPr, YCbCr, YDbDr, GS, R-GGB-G

#### Prediction Algorithms (24 types)

**Basic (16 types - Original GLIC):** NONE, CORNER, H, V, DC, DCMEDIAN, MEDIAN, AVG, TRUEMOTION, PAETH, LDIAG, HV, JPEGLS, DIFF, REF, ANGLE

**Added in C++ version (8 types):**
| Name | Description |
|------|-------------|
| SPIRAL | Spiral prediction from center |
| NOISE | Position hash-based noise |
| GRADIENT | Bilinear interpolation from 4 corners |
| MIRROR | Mirror/flip prediction |
| WAVE | Sine wave-based displacement |
| CHECKERBOARD | Alternating checkerboard prediction |
| RADIAL | Radial gradient from center |
| EDGE | Edge detection-based prediction |

**Meta predictions:** SAD, BSAD, RANDOM

#### Encoding Methods (6 types)

**Basic (3 types - Original GLIC):** raw, packed, rle

**Added in C++ version (3 types):** delta, xor, zigzag

#### Post Effects (6 types) - New in C++ version

| Name | Description |
|------|-------------|
| pixelate | Pixelation (mosaic effect) |
| scanline | Scanlines (CRT monitor style) |
| chromatic | Chromatic aberration (RGB channel offset) |
| dither | Dithering (Bayer pattern) |
| posterize | Posterize (reduce color levels) |
| glitch | Glitch shift (random row displacement) |

#### Wavelet Transforms
Haar, Daubechies (DB2-DB10), Symlet (SYM2-SYM10), Coiflet (COIF1-COIF5)

### Dependencies

- C++17 or later
- CMake 3.14 or later
- stb_image / stb_image_write (auto-downloaded during build)

---

## License

MIT License

## Acknowledgments

This project would not be possible without:

- **[GlitchCodec/GLIC](https://github.com/GlitchCodec/GLIC)** - The original GLIC implementation in Java/Processing. This C++ version is a port of their work.
- **[GLIC Documentation](https://docs.google.com/document/d/1cdJvEmSKNAkzkU0dFUa-kb_QJB2ISQg-QfCqpHLFlck/edit)** - Original documentation from GlitchCodec/GLIC
- **[nothings/stb](https://github.com/nothings/stb)** - stb_image library for image I/O
