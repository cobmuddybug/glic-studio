#!/usr/bin/env python3
"""Convert GLIC's own presets (Java-serialized slider values) to glic-studio JSON.

    build/java-harness/run.sh presets third_party/GLIC/presets glic_presets.json
    tools/convert_glic_presets.py glic_presets.json presets/glic

The mapping is GLIC's GUI.pde readValues(): list indices to ids, exponent
sliders to powers of two, radio buttons to the active item's value, and
channel 0 for every channel unless "separate channels" was on.
"""
import json
import struct
import sys
from pathlib import Path

PRED_NAMES = ["NONE", "CORNER", "H", "V", "DC", "DCMEDIAN", "MEDIAN", "AVG", "TRUEMOTION", "PAETH",
              "LDIAG", "HV", "JPEGLS", "DIFF", "REF", "ANGLE", "SAD", "BSAD", "RANDOM"]
CS_NAMES = ["OHTA", "RGB", "CMY", "HSB", "XYZ", "YXY", "HCL", "LUV", "LAB", "HWB", "R-GGB-G",
            "YPbPr", "YCbCr", "YDbDr", "Greyscale", "YUV"]
ENC_NAMES = ["RAW", "PACKED", "RLE"]


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def java_int(x):
    return int(x)  # truncation toward zero, like Java's (int) for in-range values


def radio(values, items):
    for i, v in enumerate(values):
        if v == 1.0:
            return items[i]
    return -1  # controlP5 RadioButton with nothing active


def channel(v, ch):
    pred_index = java_int(v[f"ch{ch}pred"])
    trans_index = java_int(v[f"ch{ch}trans"])
    scale_exp = f32(v[f"ch{ch}scale"])
    return {
        "min_block": 1 << java_int(v[f"ch{ch}min"]),
        "max_block": 1 << java_int(v[f"ch{ch}max"]),
        "threshold": f32(v[f"ch{ch}thr"]),
        "prediction": PRED_NAMES[pred_index],
        "quantization": java_int(v[f"ch{ch}quant"]),
        "clamp": ["none", "mod256"][radio(v[f"ch{ch}clamp"], [0, 1])],
        # wavelet list: 0 = NONE, 1..67 = ids, 68 = RANDOM
        "wavelet": -1 if trans_index == 68 else trans_index,
        "transform": {0: "FWT", 1: "WPT", -1: "RANDOM"}[radio(v[f"ch{ch}transtype"], [0, 1, -1])],
        "compression": f32(v[f"ch{ch}compress"]),
        # (int)pow(2.0, slider): Processing's pow is (float)Math.pow
        "scale": java_int(f32(2.0 ** scale_exp)),
        "encoding": ENC_NAMES[java_int(v[f"ch{ch}encoding"])],
    }


def convert(name, v):
    separate = v.get("separate_channels") == [1.0]
    channels = [channel(v, ch if separate else 0) for ch in range(3)]
    return {
        "format": "glic-studio-preset",
        "version": 1,
        "name": name,
        "source": "GLIC preset '" + name + "' (GlitchCodec/GLIC, MIT), converted by tools/convert_glic_presets.py",
        "encode": {
            "colorspace": CS_NAMES[java_int(v["colorspace"])],
            "border": [java_int(v["color_outside_r"]), java_int(v["color_outside_g"]), java_int(v["color_outside_b"])],
            "seed": 0,
            "channels": channels if separate else channels[:1],
        },
        "decode": {"linked": True},
    }


def main():
    src = json.load(open(sys.argv[1]))
    out = Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    for name, v in sorted(src.items()):
        safe = "".join(c if c.isalnum() or c in "-_." else "_" for c in name)
        (out / f"{safe}.json").write_text(json.dumps(convert(name, v), indent=2) + "\n")
    print(f"converted {len(src)} presets into {out}")


if __name__ == "__main__":
    main()
