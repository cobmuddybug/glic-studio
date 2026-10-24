// Port of GLIC's colorspaces.pde (as preprocessed by Processing: every float
// literal is a float). Colours are Java ints, 0xAARRGGBB; the alpha byte rides
// along untouched, exactly as blendRGB does in the original.

#include "colorspaces.hpp"

#include "jvm.hpp"

namespace glic {

namespace {

using namespace jvm;

inline int getR(int c) { return (c & 0xff0000) >> 16; }
inline int getG(int c) { return (c & 0xff00) >> 8; }
inline int getB(int c) { return c & 0xff; }

int getLuma(int c) {
    return constrain(toInt(0.2126f * getR(c) + 0.7152f * getG(c) + 0.0722f * getB(c)), 0, 255);
}

constexpr float r255[256] = {
    0.0f, 0.003921569f, 0.007843138f, 0.011764706f, 0.015686275f, 0.019607844f, 0.023529412f, 0.02745098f, 0.03137255f, 0.03529412f, 0.039215688f,
    0.043137256f, 0.047058824f, 0.050980393f, 0.05490196f, 0.05882353f, 0.0627451f, 0.06666667f, 0.07058824f, 0.07450981f, 0.078431375f, 0.08235294f,
    0.08627451f, 0.09019608f, 0.09411765f, 0.09803922f, 0.101960786f, 0.105882354f, 0.10980392f, 0.11372549f, 0.11764706f, 0.12156863f, 0.1254902f,
    0.12941177f, 0.13333334f, 0.13725491f, 0.14117648f, 0.14509805f, 0.14901961f, 0.15294118f, 0.15686275f, 0.16078432f, 0.16470589f, 0.16862746f,
    0.17254902f, 0.1764706f, 0.18039216f, 0.18431373f, 0.1882353f, 0.19215687f, 0.19607843f, 0.2f, 0.20392157f, 0.20784314f, 0.21176471f, 0.21568628f,
    0.21960784f, 0.22352941f, 0.22745098f, 0.23137255f, 0.23529412f, 0.23921569f, 0.24313726f, 0.24705882f, 0.2509804f, 0.25490198f, 0.25882354f,
    0.2627451f, 0.26666668f, 0.27058825f, 0.27450982f, 0.2784314f, 0.28235295f, 0.28627452f, 0.2901961f, 0.29411766f, 0.29803923f, 0.3019608f, 0.30588236f,
    0.30980393f, 0.3137255f, 0.31764707f, 0.32156864f, 0.3254902f, 0.32941177f, 0.33333334f, 0.3372549f, 0.34117648f, 0.34509805f, 0.34901962f, 0.3529412f,
    0.35686275f, 0.36078432f, 0.3647059f, 0.36862746f, 0.37254903f, 0.3764706f, 0.38039216f, 0.38431373f, 0.3882353f, 0.39215687f, 0.39607844f, 0.4f,
    0.40392157f, 0.40784314f, 0.4117647f, 0.41568628f, 0.41960785f, 0.42352942f, 0.42745098f, 0.43137255f, 0.43529412f, 0.4392157f, 0.44313726f,
    0.44705883f, 0.4509804f, 0.45490196f, 0.45882353f, 0.4627451f, 0.46666667f, 0.47058824f, 0.4745098f, 0.47843137f, 0.48235294f, 0.4862745f, 0.49019608f,
    0.49411765f, 0.49803922f, 0.5019608f, 0.5058824f, 0.50980395f, 0.5137255f, 0.5176471f, 0.52156866f, 0.5254902f, 0.5294118f, 0.53333336f, 0.5372549f,
    0.5411765f, 0.54509807f, 0.54901963f, 0.5529412f, 0.5568628f, 0.56078434f, 0.5647059f, 0.5686275f, 0.57254905f, 0.5764706f, 0.5803922f, 0.58431375f,
    0.5882353f, 0.5921569f, 0.59607846f, 0.6f, 0.6039216f, 0.60784316f, 0.6117647f, 0.6156863f, 0.61960787f, 0.62352943f, 0.627451f, 0.6313726f, 0.63529414f,
    0.6392157f, 0.6431373f, 0.64705884f, 0.6509804f, 0.654902f, 0.65882355f, 0.6627451f, 0.6666667f, 0.67058825f, 0.6745098f, 0.6784314f, 0.68235296f,
    0.6862745f, 0.6901961f, 0.69411767f, 0.69803923f, 0.7019608f, 0.7058824f, 0.70980394f, 0.7137255f, 0.7176471f, 0.72156864f, 0.7254902f, 0.7294118f,
    0.73333335f, 0.7372549f, 0.7411765f, 0.74509805f, 0.7490196f, 0.7529412f, 0.75686276f, 0.7607843f, 0.7647059f, 0.76862746f, 0.77254903f, 0.7764706f,
    0.78039217f, 0.78431374f, 0.7882353f, 0.7921569f, 0.79607844f, 0.8f, 0.8039216f, 0.80784315f, 0.8117647f, 0.8156863f, 0.81960785f, 0.8235294f, 0.827451f,
    0.83137256f, 0.8352941f, 0.8392157f, 0.84313726f, 0.84705883f, 0.8509804f, 0.85490197f, 0.85882354f, 0.8627451f, 0.8666667f, 0.87058824f, 0.8745098f,
    0.8784314f, 0.88235295f, 0.8862745f, 0.8901961f, 0.89411765f, 0.8980392f, 0.9019608f, 0.90588236f, 0.9098039f, 0.9137255f, 0.91764706f, 0.92156863f,
    0.9254902f, 0.92941177f, 0.93333334f, 0.9372549f, 0.9411765f, 0.94509804f, 0.9490196f, 0.9529412f, 0.95686275f, 0.9607843f, 0.9647059f, 0.96862745f,
    0.972549f, 0.9764706f, 0.98039216f, 0.9843137f, 0.9882353f, 0.99215686f, 0.99607843f, 1.0f,
};

inline float getNR(int c) { return r255[(c & 0xff0000) >> 16]; }
inline float getNG(int c) { return r255[(c & 0xff00) >> 8]; }
inline float getNB(int c) { return r255[c & 0xff]; }

int blendRGB(int c, int r, int g, int b) {
    return static_cast<int>((static_cast<uint32_t>(c) & 0xff000000u) | (static_cast<uint32_t>(constrain(r, 0, 255)) << 16) |
                            (static_cast<uint32_t>(constrain(g, 0, 255)) << 8) | static_cast<uint32_t>(constrain(b, 0, 255)));
}

int blendRGB(int c, float r, float g, float b) {
    return blendRGB(c, toInt(r * 255), toInt(g * 255), toInt(b * 255));
}

struct Vec3 {
    float x, y, z;
};

// ---- Greyscale
int tofromGS(int c) {
    int l = getLuma(c);
    return blendRGB(c, l, l, l);
}

// ---- YUV
constexpr float Umax = 0.436f * 255.0f;
constexpr float Vmax = 0.615f * 255.0f;

int toYUV(int c) {
    int R = getR(c), G = getG(c), B = getB(c);
    int Y = toInt(0.299f * R + 0.587f * G + 0.114f * B);
    int U = toInt(map(-0.14713f * R - 0.28886f * G + 0.436f * B, -Umax, Umax, 0, 255));
    int V = toInt(map(0.615f * R - 0.51499f * G - 0.10001f * B, -Vmax, Vmax, 0, 255));
    return blendRGB(c, Y, U, V);
}

int fromYUV(int c) {
    int Y = getR(c);
    float U = map(getG(c), 0, 255, -Umax, Umax);
    float V = map(getB(c), 0, 255, -Vmax, Vmax);
    int R = toInt(Y + 1.13983f * V);
    int G = toInt(Y - 0.39465f * U - 0.58060f * V);
    int B = toInt(Y + 2.03211f * U);
    return blendRGB(c, R, G, B);
}

// ---- YDbDr
int toYDbDr(int c) {
    int R = getR(c), G = getG(c), B = getB(c);
    int Y = toInt(0.299f * R + 0.587f * G + 0.114f * B);
    int Db = toInt(127.5f + (-0.450f * R - 0.883f * G + 1.333f * B) / 2.666f);
    int Dr = toInt(127.5f + (-1.333f * R + 1.116f * G + 0.217f * B) / 2.666f);
    return blendRGB(c, Y, Db, Dr);
}

int fromYDbDr(int c) {
    int Y = getR(c);
    float Db = (getG(c) - 127.5f) * 2.666f;
    float Dr = (getB(c) - 127.5f) * 2.666f;
    int R = toInt(Y + 9.2303716147657e-05f * Db - 0.52591263066186533f * Dr);
    int G = toInt(Y - 0.12913289889050927f * Db + 0.26789932820759876f * Dr);
    int B = toInt(Y + 0.66467905997895482f * Db - 7.9202543533108e-05f * Dr);
    return blendRGB(c, R, G, B);
}

// ---- YCbCr
int toYCbCr(int c) {
    int R = getR(c), G = getG(c), B = getB(c);
    int Y = toInt(0.2988390f * R + 0.5868110f * G + 0.1143500f * B);
    int Cb = toInt(-0.168736f * R - 0.3312640f * G + 0.5000000f * B + 127.5f);
    int Cr = toInt(0.5000000f * R - 0.4186880f * G - 0.0813120f * B + 127.5f);
    return blendRGB(c, Y, Cb, Cr);
}

int fromYCbCr(int c) {
    int Y = getR(c);
    float Cb = getG(c) - 127.5f;
    float Cr = getB(c) - 127.5f;
    int R = toInt(Y + 1.402f * Cr) + 1;  // "some fix" in the original
    int G = toInt(Y - 0.344136f * Cb - 0.714136f * Cr);
    int B = toInt(Y + 1.772000f * Cb) + 1;
    return blendRGB(c, R, G, B);
}

// ---- YPbPr
int toYPbPr(int c) {
    int R = getR(c), B = getB(c);
    int Y = getLuma(c);
    int Pb = B - Y;
    int Pr = R - Y;
    if (Pb < 0) Pb += 256;
    if (Pr < 0) Pr += 256;
    return blendRGB(c, Y, Pb, Pr);
}

int fromYPbPr(int c) {
    int Y = getR(c);
    int B = getG(c) + Y;
    int R = getB(c) + Y;
    if (R > 255) R -= 256;
    if (B > 255) B -= 256;
    int G = toInt((Y - 0.2126f * R - 0.0722f * B) / 0.7152f);
    return blendRGB(c, R, G, B);
}

// ---- R-G, G, B-G
int toRGGBG(int c) {
    int G = getG(c);
    int R = getR(c) - G;
    int B = getB(c) - G;
    if (R < 0) R += 256;
    if (B < 0) B += 256;
    return blendRGB(c, R, G, B);
}

int fromRGGBG(int c) {
    int G = getG(c);
    int R = getR(c) + G;
    int B = getB(c) + G;
    if (R > 255) R -= 256;
    if (B > 255) B -= 256;
    return blendRGB(c, R, G, B);
}

// ---- HSB
int toHSB(int c) {
    int R = getR(c), G = getG(c), B = getB(c);
    int _min = min(R, G, B);
    int _max = max(R, G, B);
    float delta = static_cast<float>(_max - _min);
    float saturation = delta / _max;
    float brightness = r255[_max];
    if (delta == 0.0f) return blendRGB(c, 0.0f, saturation, brightness);
    float hue = 0;
    if (R == _max) hue = (G - B) / delta;
    else if (G == _max) hue = 2.0f + (B - R) / delta;
    else hue = 4.0f + (R - G) / delta;
    hue /= 6.0f;
    if (hue < 0.0f) hue += 1.0f;
    return blendRGB(c, hue, saturation, brightness);
}

int fromHSB(int c) {
    float S = getNG(c);
    float B = getNB(c);
    if (S == 0.0f) return blendRGB(c, B, B, B);
    float h = 6.0f * getNR(c);
    float f = h - floor(h);
    float p = B * (1.0f - S);
    float q = B * (1.0f - S * f);
    float t = B * (1.0f - (S * (1.0f - f)));
    float r, g, b;
    switch (toInt(h)) {
    case 1: r = q; g = B; b = p; break;
    case 2: r = p; g = B; b = t; break;
    case 3: r = p; g = q; b = B; break;
    case 4: r = t; g = p; b = B; break;
    case 5: r = B; g = p; b = q; break;
    default: r = B; g = t; b = p; break;
    }
    return blendRGB(c, r, g, b);
}

// ---- HWB
int toHWB(int c) {
    int R = getR(c), G = getG(c), B = getB(c);
    int w = min(R, G, B);
    int v = max(R, G, B);
    int hue;
    if (v == w) {
        hue = 255;
    } else {
        float f = static_cast<float>((R == w) ? G - B : ((G == w) ? B - R : R - G));
        float p = (R == w) ? 3.0f : ((G == w) ? 5.0f : 1.0f);
        hue = toInt(map((p - f / (v - w)) / 6.0f, 0, 1, 0, 254));
    }
    return blendRGB(c, hue, w, 255 - v);
}

int fromHWB(int c) {
    int H = getR(c);
    int B = 255 - getB(c);
    if (H == 255) return blendRGB(c, B, B, B);
    float hue = map(H, 0, 254, 0, 6);
    float v = r255[B];
    float whiteness = getNG(c);
    int i = floor(hue);
    float f = hue - i;
    if ((i & 0x01) != 0) f = 1.0f - f;
    float n = whiteness + f * (v - whiteness);
    float r, g, b;
    switch (i) {
    case 1: r = n; g = v; b = whiteness; break;
    case 2: r = whiteness; g = v; b = n; break;
    case 3: r = whiteness; g = n; b = v; break;
    case 4: r = n; g = whiteness; b = v; break;
    case 5: r = v; g = whiteness; b = n; break;
    default: r = v; g = n; b = whiteness; break;
    }
    return blendRGB(c, r, g, b);
}

// ---- CIE helpers
constexpr float D65X = 0.950456f;
constexpr float D65Y = 1.0f;
constexpr float D65Z = 1.088754f;
constexpr float CIEEpsilon = (216.0f / 24389.0f);
constexpr float CIEK = (24389.0f / 27.0f);
constexpr float CIEK2epsilon = CIEK * CIEEpsilon;
constexpr float D65FX_4 = 4.0f * D65X / (D65X + 15.0f * D65Y + 3.0f * D65Z);
constexpr float D65FY_9 = 9.0f * D65Y / (D65X + 15.0f * D65Y + 3.0f * D65Z);
constexpr float RANGE_X = 100.0f * (0.4124f + 0.3576f + 0.1805f);
constexpr float RANGE_Y = 100.0f;
constexpr float RANGE_Z = 100.0f * (0.0193f + 0.1192f + 0.9505f);
constexpr float mepsilon = 1.0e-10f;
constexpr float corrratio = 1.0f / 2.4f;
constexpr float One_Third = 1.0f / 3.0f;
constexpr float one_hsixteen = 1.0f / 116.0f;

float correctionxyz(float n) {
    return (n > 0.04045f ? pow((n + 0.055f) / 1.055f, 2.4f) : n / 12.92f) * 100.0f;
}

Vec3 toXYZv(float rr, float gg, float bb) {
    float r = correctionxyz(rr);
    float g = correctionxyz(gg);
    float b = correctionxyz(bb);
    return {r * 0.4124f + g * 0.3576f + b * 0.1805f,
            r * 0.2126f + g * 0.7152f + b * 0.0722f,
            r * 0.0193f + g * 0.1192f + b * 0.9505f};
}

float recorrectionxyz(float n) {
    return n > 0.0031308f ? 1.055f * pow(n, corrratio) - 0.055f : 12.92f * n;
}

int fromXYZv(int c, float xx, float yy, float zz) {
    float x = xx / 100.0f;
    float y = yy / 100.0f;
    float z = zz / 100.0f;
    int r = round(255.0f * recorrectionxyz(x * 3.2406f + y * -1.5372f + z * -0.4986f));
    int g = round(255.0f * recorrectionxyz(x * -0.9689f + y * 1.8758f + z * 0.0415f));
    int b = round(255.0f * recorrectionxyz(x * 0.0557f + y * -0.2040f + z * 1.0570f));
    return blendRGB(c, r, g, b);
}

// ---- Lab
int toLAB(int c) {
    Vec3 xyz = toXYZv(getNR(c), getNG(c), getNB(c));
    xyz.x /= 100.0f;
    xyz.y /= 100.0f;
    xyz.z /= 100.0f;
    xyz.x /= D65X;
    xyz.y /= D65Y;
    xyz.z /= D65Z;
    float x, y, z;
    if (xyz.x > CIEEpsilon) x = pow(xyz.x, One_Third);
    else x = (CIEK * xyz.x + 16.0f) * one_hsixteen;
    if (xyz.y > CIEEpsilon) y = pow(xyz.y, One_Third);
    else y = (CIEK * xyz.y + 16.0f) * one_hsixteen;
    if (xyz.z > CIEEpsilon) z = pow(xyz.z, One_Third);
    else z = (CIEK * xyz.z + 16.0f) * one_hsixteen;
    float L = 255.0f * (((116.0f * y) - 16.0f) * 0.01f);
    float a = 255.0f * (0.5f * (x - y) + 0.5f);
    float b = 255.0f * (0.5f * (y - z) + 0.5f);
    return blendRGB(c, round(L), round(a), round(b));
}

int fromLAB(int c) {
    float L = 100 * getNR(c);
    float a = getNG(c) - 0.5f;
    float b = getNB(c) - 0.5f;
    float y = (L + 16.0f) * one_hsixteen;
    float x = y + a;
    float z = y - b;
    float xxx = x * x * x;
    if (xxx > CIEEpsilon) x = xxx;
    else x = (116.0f * x - 16.0f) / CIEK;
    float yyy = y * y * y;
    if (yyy > CIEEpsilon) y = yyy;
    else y = L / CIEK;
    float zzz = z * z * z;
    if (zzz > CIEEpsilon) z = zzz;
    else z = (116.0f * z - 16.0f) / CIEK;
    return fromXYZv(c, RANGE_X * x, RANGE_Y * y, RANGE_Z * z);
}

// ---- Luv
float PerceptibleReciprocal(float x) {
    float sgn = x < 0.0f ? -1.0f : 1.0f;
    if ((sgn * x) >= mepsilon) return (1.0f / x);
    return (sgn / mepsilon);
}

int toLUV(int c) {
    Vec3 xyz = toXYZv(getNR(c), getNG(c), getNB(c));
    xyz.x /= 100.0f;
    xyz.y /= 100.0f;
    xyz.z /= 100.0f;
    float d = xyz.y;
    float L;
    if (d > CIEEpsilon) L = 116.0f * pow(d, One_Third) - 16.0f;
    else L = CIEK * d;
    float alpha = PerceptibleReciprocal(xyz.x + 15.0f * xyz.y + 3.0f * xyz.z);
    float L13 = 13.0f * L;
    float u = L13 * ((4.0f * alpha * xyz.x) - D65FX_4);
    float v = L13 * ((9.0f * alpha * xyz.y) - D65FY_9);
    L /= 100.0f;
    u = (u + 134.0f) / 354.0f;
    v = (v + 140.0f) / 262.0f;
    return blendRGB(c, round(L * 255), round(u * 255), round(v * 255));
}

int fromLUV(int c) {
    float L = 100.0f * getNR(c);
    float u = 354.0f * getNG(c) - 134.0f;
    float v = 262.0f * getNB(c) - 140.0f;
    float X, Y, Z;
    if (L > CIEK2epsilon) Y = pow((L + 16.0f) * one_hsixteen, 3.0f);
    else Y = L / CIEK;
    float L13 = 13.0f * L;
    float L52 = 52.0f * L;
    float Y5 = 5.0f * Y;
    float L13u = L52 / (u + L13 * D65FX_4);
    X = ((Y * ((39.0f * L / (v + L13 * D65FY_9)) - 5.0f)) + Y5) / ((((L13u) - 1.0f) / 3.0f) + One_Third);
    Z = (X * (((L13u) - 1.0f) / 3.0f)) - Y5;
    return fromXYZv(c, 100 * X, 100 * Y, 100 * Z);
}

// ---- HCL
int toHCL(int c) {
    float r = getNR(c);
    float g = getNG(c);
    float b = getNB(c);
    float mx = max(r, max(g, b));
    float chr = mx - min(r, min(g, b));
    float h = 0.0f;
    if (chr != 0) {
        if (r == mx) h = fmod((g - b) / chr + 6.0f, 6.0f);
        else if (g == mx) h = (b - r) / chr + 2.0f;
        else h = (r - g) / chr + 4.0f;
    }
    return blendRGB(c, round((h / 6.0f) * 255), round(chr * 255), round(255 * (0.298839f * r + 0.586811f * g + 0.114350f * b)));
}

int fromHCL(int c) {
    float h = 6.0f * getNR(c);
    float chr = getNG(c);
    float l = getNB(c);
    float x = chr * (1.0f - abs(fmod(h, 2.0f) - 1.0f));
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if ((0.0f <= h) && (h < 1.0f)) { r = chr; g = x; }
    else if ((1.0f <= h) && (h < 2.0f)) { r = x; g = chr; }
    else if ((2.0f <= h) && (h < 3.0f)) { g = chr; b = x; }
    else if ((3.0f <= h) && (h < 4.0f)) { g = x; b = chr; }
    else if ((4.0f <= h) && (h < 5.0f)) { r = x; b = chr; }
    else { r = chr; b = x; }
    float m = l - (0.298839f * r + 0.586811f * g + 0.114350f * b);
    return blendRGB(c, round(255 * (r + m)), round(255 * (g + m)), round(255 * (b + m)));
}

// ---- Yxy
int toYXY(int c) {
    Vec3 xyz = toXYZv(getNR(c), getNG(c), getNB(c));
    float sum = xyz.x + xyz.y + xyz.z;
    float x = xyz.x > 0 ? xyz.x / sum : 0.0f;
    float y = xyz.y > 0 ? xyz.y / sum : 0.0f;
    return blendRGB(c, toInt(map(xyz.y, 0, RANGE_Y, 0, 255)), toInt(map(x, 0.0f, 1.0f, 0, 255)),
                    toInt(map(y, 0.0f, 1.0f, 0, 255)));
}

int fromYXY(int c) {
    float Y = map(getR(c), 0, 255, 0, RANGE_Y);
    float x = map(getG(c), 0, 255, 0, 1.0f);
    float y = map(getB(c), 0, 255, 0, 1.0f);
    float divy = Y / (y > 0 ? y : 1.0e-6f);
    return fromXYZv(c, x * divy, Y, (1 - x - y) * divy);
}

// ---- XYZ
int toXYZ(int c) {
    Vec3 xyz = toXYZv(getNR(c), getNG(c), getNB(c));
    return blendRGB(c, toInt(map(xyz.x, 0, RANGE_X, 0, 255)), toInt(map(xyz.y, 0, RANGE_Y, 0, 255)),
                    toInt(map(xyz.z, 0, RANGE_Z, 0, 255)));
}

int fromXYZ(int c) {
    float x = map(getR(c), 0, 255, 0, RANGE_X);
    float y = map(getG(c), 0, 255, 0, RANGE_Y);
    float z = map(getB(c), 0, 255, 0, RANGE_Z);
    return fromXYZv(c, x, y, z);
}

// ---- CMY
int toCMY(int c) { return blendRGB(c, 255 - getR(c), 255 - getG(c), 255 - getB(c)); }
int fromCMY(int c) { return toCMY(c); }

// ---- OHTA
int fromOHTA(int c) {
    int I1 = getR(c);
    float I2 = map(getG(c), 0, 255, -127.5f, 127.5f);
    float I3 = map(getB(c), 0, 255, -127.5f, 127.5f);
    int R = toInt(I1 + 1.00000f * I2 - 0.66668f * I3);
    int G = toInt(I1 + 1.33333f * I3);
    int B = toInt(I1 - 1.00000f * I2 - 0.66668f * I3);
    return blendRGB(c, R, G, B);
}

int toOHTA(int c) {
    int R = getR(c), G = getG(c), B = getB(c);
    int I1 = toInt(0.33333f * R + 0.33334f * G + 0.33333f * B);
    int I2 = toInt(map(0.5f * (R - B), -127.5f, 127.5f, 0, 255));
    int I3 = toInt(map(-0.25000f * R + 0.50000f * G - 0.25000f * B, -127.5f, 127.5f, 0, 255));
    return blendRGB(c, I1, I2, I3);
}

} // namespace

int toColorspace(int c, int cs) {
    switch (cs) {
    case 0: return toOHTA(c);
    case 2: return toCMY(c);
    case 4: return toXYZ(c);
    case 5: return toYXY(c);
    case 6: return toHCL(c);
    case 7: return toLUV(c);
    case 8: return toLAB(c);
    case 9: return toHWB(c);
    case 3: return toHSB(c);
    case 10: return toRGGBG(c);
    case 11: return toYPbPr(c);
    case 12: return toYCbCr(c);
    case 13: return toYDbDr(c);
    case 15: return toYUV(c);
    case 14: return tofromGS(c);
    default: return c;
    }
}

int fromColorspace(int c, int cs) {
    switch (cs) {
    case 0: return fromOHTA(c);
    case 2: return fromCMY(c);
    case 4: return fromXYZ(c);
    case 5: return fromYXY(c);
    case 6: return fromHCL(c);
    case 7: return fromLUV(c);
    case 8: return fromLAB(c);
    case 9: return fromHWB(c);
    case 3: return fromHSB(c);
    case 10: return fromRGGBG(c);
    case 11: return fromYPbPr(c);
    case 12: return fromYCbCr(c);
    case 13: return fromYDbDr(c);
    case 14: return tofromGS(c);
    case 15: return fromYUV(c);
    default: return c;
    }
}

} // namespace glic
