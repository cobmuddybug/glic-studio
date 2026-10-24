# Test images

| File | Size | Content | Origin |
|---|---|---|---|
| photo.png | 640×425 | Photograph (Hallett Peak, RMNP) | U.S. NPS, public domain (see NOTICE.md); downscaled with ffmpeg lanczos |
| flat.png | 400×300 | Flat-colour shapes | `tools/gen_test_images.py` |
| gradient.png | 512×256 | Smooth gradient | `tools/gen_test_images.py` |
| contrast.png | 300×300 | Checkerboard, stripes, thin lines | `tools/gen_test_images.py` |
| small64.png | 64×64 | Mixed small image | `tools/gen_test_images.py` |

These are inputs to the golden tests. Changing one invalidates `tests/baseline/`.
