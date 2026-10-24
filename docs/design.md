# glic-studio — Design Document

A native Wayland desktop editor for GLIC (GLitch Image Codec). It wraps the C++ port of GLIC in a live, interactive GUI built around exploring the codec's internals rather than hiding them.

Working name: `glic-studio` (rename freely).
Project location: `~/Projects/glic-studio/`
Version control: git, pushed to a private GitLab repo.

---

## 0. Before writing any code

These steps come first, in this order.

1. **Check licensing.** Clone `GlitchCodec/GLIC` (the original Java/Processing version) and read its LICENSE file. Clone `daitomanabe/glic-cpp` (the C++ port, which is labelled MIT). Record both in `NOTICE.md`.
   - If the original is permissive, glic-studio is MIT.
   - If the original is GPL, glic-studio is GPL-3.0-or-later, and the port is treated as GPL too.
   - If the original has no licence, stop and ask the project owner, who will ask the author before going further.
2. **Build and verify glic-cpp** on Fedora, unmodified. Encode and decode at least five test images (photo, flat graphic, gradient, high-contrast, small 64×64). Save the outputs in `tests/baseline/`. This is the ground truth for whether later refactors changed behaviour.
3. **Read the glic-cpp source** and write `docs/glic-internals.md`. It should cover the pipeline stages, where segmentation, prediction, quantization, wavelet transforms and encoding happen, which data is available at each stage, and how the `.glic` file is laid out. The overlay and corruption features depend on this, so do it properly before designing the library API.

---

## 1. Goals and non-goals

### Goals
- Live preview of GLIC encode/decode with immediate feedback.
- Encode and decode settings are separate, so mismatched-parameter glitching is a first-class feature.
- Visual overlays that show the codec's internals (segmentation, prediction choice, wavelet bands).
- Deterministic, reproducible results. Every output can be regenerated exactly from its preset.
- A plain JSON preset format that the bundled CLI can also run, for batch or scripted renders.
- A native Wayland app that behaves well in a scrolling/tiling compositor (niri).
- Keyboard-friendly, with mnemonic letter keys and no reliance on F-keys.

### Non-goals (for now)
- Windows/macOS builds (don't break them deliberately, but don't test them).
- Layers, masks, painting, or general image editing. This is a GLIC tool, not GIMP.
- Real-time video or webcam input.
- Plugin systems or scripting.

---

## 2. Target platform

| Item | Choice |
|---|---|
| OS | Fedora (current release) |
| Compositor | niri (scrolling tiling Wayland), with the DMS shell |
| Display | Native Wayland. No XWayland dependency. |
| GPU | AMD integrated (Radeon 680M) via Mesa |
| Language | C++20 |
| Build | CMake ≥ 3.20, Ninja |

### Wayland and niri considerations
- The window must work at **any size and aspect ratio**, since niri sizes columns. No minimum size above about 640×400. The layout should reflow, not clip.
- **No floating modal dialogs** for core workflows. Use in-window panels and popups instead.
- **File dialogs go through xdg-desktop-portal** (SDL3's `SDL_ShowOpenFileDialog` / `SDL_ShowSaveFileDialog` do this). Drag-and-drop of image files onto the window must also work.
- Set the app ID to `glic-studio` so niri window rules can target it. Ship a `.desktop` file.
- Respect fractional scaling (SDL3 display scale, multiplied by the ImGui font and style scale).

---

## 3. Technology

| Layer | Choice | Licence |
|---|---|---|
| Windowing/input | SDL3 (Wayland backend) | zlib |
| Rendering | OpenGL 3.3 core via SDL3 | n/a |
| UI | Dear ImGui (docking branch) | MIT |
| Image I/O | stb_image / stb_image_write (already used by glic-cpp) | PD/MIT |
| PNG metadata | lodepng, for writing and reading `tEXt`/`iTXt` chunks | zlib |
| JSON | nlohmann/json | MIT |
| Tests | Catch2 or doctest | BSL/MIT |
| Video/GIF export | Optional external `ffmpeg` binary, detected at runtime | n/a (not linked) |

Use the Fedora packages for SDL3 if they're available. Otherwise vendor everything with CMake `FetchContent`, pinned to specific tags or commits.

---

## 4. Architecture

```
glic-studio/
├── libglic/          # glic-cpp refactored into a library (behaviour-identical)
├── glic-cli/         # original CLI, now a thin wrapper over libglic
├── app/              # the GUI
│   ├── ui/           # ImGui panels
│   ├── render/       # background render worker, texture upload
│   ├── model/        # Preset, History, Project state
│   └── platform/     # SDL3 window, portal dialogs, DnD
├── presets/          # bundled example presets (.json)
├── tests/
│   ├── baseline/     # golden outputs from unmodified glic-cpp
│   └── ...
└── docs/
```

### 4.1 libglic
- Refactor glic-cpp so its encode/decode is callable as a library. **Output must stay byte-identical** to the baseline (enforced by tests).
- Core API, roughly:
  ```cpp
  EncodeResult glic::encode(const Image&, const EncodeSettings&, const Hooks* = nullptr);
  Image        glic::decode(const GlicData&, const DecodeSettings&, const Hooks* = nullptr);
  ```
- `Hooks` is an optional struct of callbacks or capture targets for the overlays. It records the segmentation tree, the prediction method chosen per block, and the wavelet coefficients per channel. With no hooks attached there should be zero overhead.
- `GlicData` is the in-memory encoded representation, and it can be serialized to and from `.glic` bytes. The corruption lab works on those bytes.
- Rendering must be cancellable, through a `std::atomic<bool>` or generation counter that's checked between blocks and stages.

### 4.2 glic-cli
- Keep the existing CLI behaviour.
- Add `--preset file.json`, which applies a full preset including corruption and seed. This gives you headless batch rendering with exactly the same output as the GUI.

### 4.3 Render worker
- One background thread owns rendering. The UI thread never blocks on GLIC.
- Every settings change produces a new **render request** with an incrementing generation number. The worker drops stale requests and aborts a render in progress when a newer one arrives.
- **Two-tier preview:**
  - While a slider is being dragged, render a downscaled proxy (longest edge 512 px, configurable).
  - When input settles (about 150 ms with no change), render at full resolution.
- Cache stage outputs where it's safe to. If only decode settings changed, reuse the existing `GlicData` and skip re-encoding. That's the main speed win for mismatch exploration.

---

## 5. Data model

### 5.1 Preset (JSON, versioned)

```json
{
  "format": "glic-studio-preset",
  "version": 1,
  "name": "spiral dusk",
  "encode": {
    "colorspace": "HWB",
    "channels": [
      { "min_block": 2, "max_block": 256, "threshold": 15,
        "prediction": "SPIRAL", "quantization": 180,
        "wavelet": "HAAR", "transform_scale": 20, "encoding": "PACKED" },
      { "...": "channel 2" },
      { "...": "channel 3" }
    ]
  },
  "decode": {
    "linked": true,
    "colorspace": "HWB",
    "channels": [ "... same shape, ignored when linked ..." ]
  },
  "corruption": {
    "enabled": false,
    "seed": 1337,
    "mode": "random_bytes",
    "rate": 0.0005,
    "regions": [ { "start": 0.40, "end": 0.55, "rate": 0.01 } ],
    "protect_header": true
  },
  "post_effects": [ { "type": "chromatic", "offset": [2, 0] } ]
}
```

- The field names above are placeholders. **Match them to the parameters glic-cpp actually exposes**, as documented in `docs/glic-internals.md`.
- Unknown fields are kept on load and re-saved, for forward compatibility.
- `regions` positions are fractions of the payload length, so presets work on any image size.

### 5.2 Determinism
- All randomness (corruption, randomize, mutate) uses one explicitly seeded PRNG (PCG32 or `std::mt19937_64`) that's passed down. **No `rand()`, time-based seeds, or hidden global state.**
- Same input image + same preset = byte-identical output, on every run.

### 5.3 Embedded presets
- On PNG export, embed the full preset JSON in an `iTXt` chunk with the keyword `glic-studio-preset`.
- When a PNG with that chunk is dropped onto the window, offer two choices in an inline bar (not a modal): "Load settings" or "Load as source image".

### 5.4 History
- History is a **branching tree**, not a linear undo stack. Each node holds a preset snapshot and a small thumbnail.
- Undo/redo moves along the current branch. Making a change after an undo starts a new branch instead of throwing away the old one.
- History is kept in memory per session. Saving it to disk is optional and can come later.

---

## 6. UI

### 6.1 Style
- Restrained and functional. Use the default ImGui dark style with small adjustments: no decorative chrome, no card-heavy layouts, no ornamental theming.
- Use a dockable layout, but ship one sensible default and a "Reset layout" command.

### 6.2 Default layout

```
┌─────────────────────────────────────┬────────────────┐
│                                     │  Settings      │
│           Canvas                    │  [Encode|Decode│
│   (preview / original / split)      │   |Corrupt|FX] │
│                                     │                │
│                                     │  per-channel   │
│                                     │  columns       │
├─────────────────────────────────────┤                │
│  History strip (thumbnails)         │                │
└─────────────────────────────────────┴────────────────┘
 status: render time · resolution · seed · proxy/full
```

In narrow niri columns, the settings panel collapses below the canvas.

### 6.3 Canvas
- Zoom (wheel, `+`/`-`, `0` = fit, `1` = 100%) and pan (middle-drag or space-drag).
- View modes: glitched, original, split slider, and difference.
- Overlays can be toggled independently on top of any view mode (see M4).

### 6.4 Settings panel
- Tabs: **Encode**, **Decode**, **Corrupt**, **Effects**.
- Encode and Decode each show **three channel columns** with a "link channels" toggle.
- The Decode tab has a large **"Linked to encode"** toggle at the top. When it's unlinked, a subtle "mismatch" badge appears in the status bar.
- Every control can be reset with right-click → "Reset to default" and "Copy value to all channels".

### 6.5 Keybindings (mnemonic letters, no F-keys)

| Key | Action |
|---|---|
| `Ctrl+O` | Open image |
| `Ctrl+S` | Export PNG (with embedded preset) |
| `Ctrl+Shift+S` | Save preset JSON |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo (current branch) |
| `R` | Randomize all settings |
| `M` | Open mutate grid |
| `L` | Toggle decode linked/unlinked |
| `C` | Toggle corruption |
| `N` | New corruption seed (reroll) |
| `Tab` | Cycle view mode |
| `O` | Toggle overlays panel |
| `Space` (hold) | Show original while held |
| `Ctrl+C` | Copy current render to clipboard |

Keybindings live in a single table in code so they're easy to change later.

---

## 7. Milestones

Work through these in order. Each milestone must build, pass its tests, and be committed before the next one starts.

### M0 — Foundation
- Licence check, baseline build and internals doc (section 0).
- Refactor into `libglic` + `glic-cli`. Golden-image tests pass byte-for-byte against `tests/baseline/`.

### M1 — Core loop
- SDL3/ImGui window on native Wayland, with the app ID set.
- Open an image (portal dialog and drag-and-drop) and show it on the canvas.
- Encode settings panel, render worker, proxy/full previews, cancellation.
- View modes: glitched, original, split, plus hold-Space.
- Export PNG.

### M2 — Encode/decode split
- Decode tab with linked/unlinked modes.
- Decode-only changes reuse the cached `GlicData`.

### M3 — Presets and reproducibility
- Preset save/load and the bundled example presets.
- PNG metadata embed and read back.
- `glic-cli --preset`.
- Test: preset → GUI render == preset → CLI render, byte-identical.

### M4 — Codec overlays
- Segmentation overlay (block outlines, colour-coded by depth).
- Prediction-method heatmap, per channel, with a legend.
- Wavelet band view for the selected channel.
- Overlays use `Hooks` data and are captured only while their toggle is on.

### M5 — Corruption lab
- Random-bytes mode with seed and rate. Header protection is on by default.
- Byte map: a horizontal strip showing the payload, where you click-drag to add regions with their own rates.
- `N` rerolls the seed.
- Corrupted data that fails to decode must **never crash** the app. Catch the failure, show "decode failed" in the status bar, and keep the last good render on screen.

### M6 — Exploration
- Randomize, with constraints so it doesn't produce unusable extremes (sensible ranges per parameter, documented in code).
- **Mutate grid:** a 3×3 grid of proxy renders, each a small perturbation of the current preset. Click one to adopt it, which becomes a history node. Mutation strength is set with a slider.
- Branching history strip with thumbnails.

### M7 — Animation export
- Pick one parameter (or the corruption seed), a start value, end value, frame count and easing.
- Render frames to a temp directory as PNG.
- If `ffmpeg` is on PATH, offer MP4 and GIF export. Otherwise just export the PNG sequence and say why.

### M8 — Polish
- Settings persistence (window layout, recent files, last preset) in `~/.config/glic-studio/`.
- A `.desktop` file and app icon.
- README covering build, usage and licences.

---

## 8. Testing

- **Golden-image tests** against `tests/baseline/` for libglic after every refactor.
- **Determinism tests:** render the same preset twice and compare bytes, including with corruption enabled.
- **Preset round-trip:** load → save → load gives an identical struct, and unknown fields are preserved.
- **PNG metadata round-trip.**
- **Corruption fuzz:** 1,000 random seeds at high corruption rates must produce no crashes, hangs over a timeout, or memory errors. Run under ASan/UBSan in debug builds.
- GUI code is tested by hand. Keep logic out of the UI layer so it stays testable.

---

## 9. Performance targets

These are rough targets on the development machine (Ryzen 9 6900HX):
- Proxy render (512 px) in under 100 ms for typical settings.
- A decode-only change on a 2-megapixel image in under 500 ms.
- The UI stays at 60 fps while rendering, because rendering never happens on the UI thread.

If GLIC itself is too slow for these targets, profile first. Parallelize per channel before anything clever.

---

## 10. Open questions (ask the project owner, don't guess)

- The final app name.
- Whether the post-effects from glic-cpp should stay, or the app should stay pure GLIC.
- How much the history tree should persist to disk between sessions.

---

## 11. Notes for Claude Code

- Work milestone by milestone. Don't jump ahead.
- After each milestone, summarize what changed and what's untested, then commit.
- **Never change libglic's output** without a golden-test update that the project owner has explicitly approved.
- When glic-cpp's actual structure conflicts with this document, the code is right. Update this document and note the change.
- Keep the UI restrained. If a feature seems to need a new panel, first consider whether it fits as a tab, toggle or popup.

---

## 12. Changes from this document (recorded during M0, 2026-10-25)

Per §11, where the code disagreed with the plan, the code won:

- **Base codec.** §0 and §4.1 assumed `glic-cpp` would be refactored into libglic. It was
  (commit `735d39e`, byte-identical to upstream), but upstream turned out to be broken: crashes
  on REF/SAD/BSAD/RANDOM, inverted signs in its bit reader, a literal scale of 20, and
  non-invertible wavelets. Its default output is near-flat grey. With the project owner's agreement,
  libglic is instead a **direct port of the original Java GLIC**, verified byte-for-byte against
  GLIC run headless (`tools/java-harness`). glic-cpp's extra predictors, encodings and post
  effects are not part of GLIC and are gone. That answers §10's post-effects question for now:
  pure GLIC.
- **Baselines** (§0.2) come from the original GLIC, not glic-cpp: 134 cases × up to 6 images.
  Test images: photo (public-domain NPS, 384 px), flat, gradient, high-contrast, 64×64, and a
  37×23 image below GLIC's 64 px decode minimum.
- **File format** is GLIC's own `.glic` (header + marks + 512-byte separators), not glic-cpp's.
- **Parameters** (§5.1 placeholders) are GLIC's: colour space, border, and per channel min/max
  block, threshold, prediction (incl. SAD/BSAD/RANDOM), quantization, clamp, wavelet (67 JWave
  filters), transform type (FWT/WPT), compression, scale, encoding (raw/packed/rle). Decode-side
  values are prediction, quantization, clamp, wavelet, transform type, scale, encoding, colour
  space and border. Mismatch decoding is GLIC's own "skip header" mode.
- **Determinism** (§5.2): GLIC draws everything from Processing's `random()`. libglic reproduces
  `java.util.Random` exactly and seeds it per call from the preset's seed, so the GUI, the CLI
  and GLIC itself all agree.
- **API** (§4.1): `glic::encode` returns `EncodeResult` and `glic::decode` returns
  `DecodeResult` (both carry ok/cancelled/error), because failures and cancellation are
  first-class. `GlicData` is the exact `.glic` byte buffer, and `layout()` describes its
  sections for the corruption lab.
- **Platform**: development happens on Omarchy/Hyprland. Nothing is distro-specific, but niri
  and Fedora are untested until someone runs them.

### Recorded during M1

- **Preview scheduling** (§4.3): changes made while a proxy render is in flight are coalesced.
  The next proxy starts as soon as the current one finishes, rather than cancelling it, so slow
  settings such as SAD still update during a drag. A full render in flight is cancelled by any
  change. The logic lives in `app/model/preview_scheduler.*` and is unit-tested.
- **Export** writes the full-resolution render cropped to the source size. GLIC's own decoder
  pads sides below 64 px; the canvas and the export both hide that padding.
- **Layout**: the settings panel docks to the right in wide windows and below the canvas in
  narrow or portrait ones. Crossing the threshold rearranges it ("View → Rearrange for window
  shape", on by default). "Reset layout" restores the default. ImGui's `imgui.ini` is disabled
  until settings persistence lands in M8.
- **Test hook**: `glic-studio --open IMG --spec ... --view MODE --capture OUT.png` screenshots
  the window once the full render is on screen, then exits. `tools/gui_smoke.sh` uses it.
- Only the Encode tab exists so far. Decode, Corrupt and Effects arrive with their milestones
  rather than as empty tabs.

### Recorded during M2

- **Decode tab**: "Linked to encode" decodes with the header. Unlinking starts from exactly the
  header's values, so the image doesn't change until you edit something. Unlinked decoding is
  GLIC's skip-header mode. The decode prediction list offers only concrete predictors, because
  GLIC turns meta predictors into NONE at decode. "Reset to encoded value" replaces "Reset to
  default" for decode controls. While linked, the tab shows the header values read-only,
  including resolved random wavelets.
- **Reuse**: the render worker caches the last encode, for proxy and full size separately, keyed
  on (source, encode settings). Decode-only changes skip encoding, and the status bar says
  "decode only". This is unit-tested.
