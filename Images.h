#pragma once

#include "Math.h"

#include <vector>

#include <stdint.h>
typedef uint8_t uint8;
typedef uint32_t uint32;

inline float PixelToUV(int pixel, int width)
{
    // x' = (x+0.5)/w
    return (float(pixel) + 0.5f) / float(width);
}

inline int UVToPixel(float uv, int width)
{
    // x = x'*w-0.5
    // You would then round to the nearest int, which means you would add 0.5 and cast to int. So:
    // x = int(x'*w)
    // adding 1 to make slightly negative locations go positive. should probably handle negatives more robustly!
    return int((uv + 1.0f)*float(width));
}

inline int UVToPixel(float uv, int width, float& fract)
{
    // same as above, but gives you the fractional pixel value which is useful for interpolation
    float x = (uv + 1.0f) * float(width) - 0.5f;
    fract = std::fmodf(x, 1.0f);
    return int(x - fract + 0.5f);
}

enum class SampleType
{
    Nearest,
    Linear,
    LinearMip
};

struct RGBU8
{
    uint8 r = 0;
    uint8 g = 0;
    uint8 b = 0;
};

struct RGBF32
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

inline RGBF32 operator += (RGBF32& a, const RGBF32& b)
{
    a.r += b.r;
    a.g += b.g;
    a.b += b.b;
    return a;
}

inline RGBF32 operator *= (RGBF32& a, float f)
{
    a.r *= f;
    a.g *= f;
    a.b *= f;
    return a;
}

inline RGBU8 operator * (const RGBU8& a, float f)
{
    RGBU8 ret;
    ret.r = uint8(float(a.r) * f);
    ret.g = uint8(float(a.g) * f);
    ret.b = uint8(float(a.b) * f);
    return ret;
}

inline RGBU8 operator + (const RGBU8& a, const RGBU8& b)
{
    RGBU8 ret;
    ret.r = a.r + b.r;
    ret.g = a.g + b.g;
    ret.b = a.b + b.b;
    return ret;
}

struct Image
{
    int width, height;
    std::vector<RGBU8> pixels;
};

typedef std::vector<Image> ImageMips;

inline RGBU8 SampleNearest (const Image& image, const Vector2& uv)
{
    int x = UVToPixel(uv[0], image.width) % image.width;
    int y = UVToPixel(uv[1], image.height) % image.height;

    return image.pixels[y*image.width + x];
}

inline RGBU8 SampleBilinear(const Image& image, const Vector2& uv)
{
    float xweight, yweight;

    int x0 = UVToPixel(uv[0], image.width, xweight) % image.width;
    int y0 = UVToPixel(uv[1], image.height, yweight) % image.height;
    int x1 = (x0 + 1) % image.width;
    int y1 = (y0 + 1) % image.height;

    RGBU8 p00 = image.pixels[y0*image.width + x0];
    RGBU8 p10 = image.pixels[y0*image.width + x1];
    RGBU8 p01 = image.pixels[y1*image.width + x0];
    RGBU8 p11 = image.pixels[y1*image.width + x1];

    RGBU8 px0 = lerp(p00, p10, xweight);
    RGBU8 px1 = lerp(p01, p11, xweight);

    return lerp(px0, px1, yweight);
}

inline RGBU8 SampleTrilinear(const ImageMips& texture, const Vector2& uv, float mip)
{
    RGBU8 bilinearLowMip = SampleBilinear(texture[std::min(int(mip), (int)texture.size() - 1)], uv);
    RGBU8 bilinearHighMip = SampleBilinear(texture[std::min(int(mip) + 1, (int)texture.size() - 1)], uv);
    return lerp(bilinearLowMip, bilinearHighMip, std::fmodf(mip, 1.0f));
}

// using a gamma of 2.2
inline float sRGBU8_To_LinearFloat(uint8 in)
{
    float ret = float(in) / 255.0f;
    return std::powf(ret, 2.2f);
}

inline uint8 LinearFloat_To_sRGBU8(float in)
{
    in = std::powf(in, 1.0f / 2.2f);
    return uint8(clamp(in * 255.0f + 0.5f, 0.0f, 255.0f));
}

inline RGBF32 RGB_U8_To_F32(const RGBU8& rgbu8)
{
    RGBF32 ret;
    ret.r = sRGBU8_To_LinearFloat(rgbu8.r);
    ret.g = sRGBU8_To_LinearFloat(rgbu8.g);
    ret.b = sRGBU8_To_LinearFloat(rgbu8.b);
    return ret;
}

inline RGBU8 RGB_F32_To_U8(const RGBF32& rgbf32)
{
    RGBU8 ret;
    ret.r = LinearFloat_To_sRGBU8(rgbf32.r);
    ret.g = LinearFloat_To_sRGBU8(rgbf32.g);
    ret.b = LinearFloat_To_sRGBU8(rgbf32.b);
    return ret;
}
