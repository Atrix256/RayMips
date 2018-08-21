#define _CRT_SECURE_NO_WARNINGS

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <stdint.h>
typedef uint8_t uint8;
typedef uint32_t uint32;

#include <vector>
#include <algorithm>
#include <array>

typedef std::array<float, 2> Vector2;

typedef std::array<Vector2, 2> Matrix22;
// indexed as [row][column]
// [0][0] = row0 col0
// [0][1] = row0 col1
// [1][0] = row1 col0
// [1][1] = row1 col1

static const Matrix22 c_identity22 = 
{
    {
        {1.0f, 0.0f},
        {0.0f, 1.0f}
    }
};

Vector2 operator * (const Vector2& p, const Matrix22& m)
{
    Vector2 ret;
    ret[0] = p[0] * m[0][0] + p[1] * m[1][0];
    ret[1] = p[0] * m[0][1] + p[1] * m[1][1];
    return ret;
}

Matrix22 operator * (const Matrix22& a, const Matrix22& b)
{
    Matrix22 ret;
    for (int i = 0; i < 2; ++i)
    {
        for (int j = 0; j < 2; ++j)
        {
            ret[i][j] = a[i][j] * b[j][i];
        }
    }
    return ret;
}

float Dot(const Vector2& a, const Vector2& b)
{
    return
        a[0] * b[0] +
        a[1] * b[1];
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

template <typename T>
inline T clamp(T v, T min, T max)
{
    if (v < min)
        return min;
    else if (v > max)
        return max;
    else return v;
}

template <typename T>
inline T lerp(const T& a, const T& b, float t)
{
    return a * (1.0f - t) + b * t;
}

RGBU8 SampleNearest (const Image& image, const Vector2& uv)
{
    float xfloat = uv[0] * float(image.width);
    float yfloat = uv[1] * float(image.height);

    int x = int(xfloat + 0.5f) % (image.width);
    int y = int(yfloat + 0.5f) % (image.height);

    return image.pixels[y*image.width + x];
}

RGBU8 SampleBilinear(const Image& image, const Vector2& uv)
{
    float xfloat = uv[0] * float(image.width);
    float yfloat = uv[1] * float(image.height);

    float xweight = std::fmodf(xfloat, 1.0f);
    float yweight = std::fmodf(yfloat, 1.0f);

    int x0 = int(xfloat + 0.5f) % (image.width);
    int y0 = int(yfloat + 0.5f) % (image.height);

    int x1 = (x0 + 1) % (image.width);
    int y1 = (y0 + 1) % (image.height);

    RGBU8 p00 = image.pixels[y0*image.width + x0];
    RGBU8 p10 = image.pixels[y0*image.width + x1];
    RGBU8 p01 = image.pixels[y1*image.width + x0];
    RGBU8 p11 = image.pixels[y1*image.width + x1];

    RGBU8 px0 = lerp(p00, p10, xweight);
    RGBU8 px1 = lerp(p01, p11, xweight);

    return lerp(px0, px1, yweight);
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

// Make mips of an image, using a box filter. This is a common way to make mips.
void MakeMips(ImageMips& mips, const uint8* pixels, int width, int height)
{
    // calculate how many mips we need to make the longer axis reach 1 pixel in size.
    int largestAxis = std::max(width, height);
    int numMips = 0;
    while (largestAxis)
    {
        largestAxis = largestAxis >> 1;
        numMips++;
    }
    mips.resize(numMips);

    // copy the full sized image as the first mip
    mips[0].width = width;
    mips[0].height = height;
    mips[0].pixels.resize(width*height);
    memcpy(mips[0].pixels.data(), pixels, width*height * sizeof(RGBU8));

    // make the rest of the mips
    for (int mipIndex = 1; mipIndex < numMips; ++mipIndex)
    {
        int srcWidth = mips[mipIndex - 1].width;
        int srcHeight = mips[mipIndex - 1].height;

        int destWidth = std::max(srcWidth / 2, 1);
        int destHeight = std::max(srcHeight / 2, 1);

        int widthRatio = srcWidth / destWidth;
        int heightRatio = srcHeight / destHeight;

        // set up the mip data and allocate memory for the pixels
        mips[mipIndex].width = destWidth;
        mips[mipIndex].height = destHeight;
        mips[mipIndex].pixels.resize(destWidth*destHeight);

        const RGBU8* srcPixels = mips[mipIndex-1].pixels.data();
        RGBU8* destPixel = mips[mipIndex].pixels.data();

        for (int y = 0; y < destHeight; ++y)
        {
            for (int x = 0; x < destWidth; ++x)
            {
                // Make a mip pixel by averaging the 4 contributing pixels of the source image, and do it in linera space, not sRGB
                // Due to the std::max call, it may not be 4 pixels contributing though.
                int sampleCount = 0;
                RGBF32 linearColor;
                for (int iy = 0; iy < heightRatio; ++iy)
                {
                    for (int ix = 0; ix < widthRatio; ++ix)
                    {
                        linearColor += RGB_U8_To_F32(srcPixels[(y * heightRatio + iy)*srcWidth + (x * widthRatio + ix)]);
                        ++sampleCount;
                    }
                }
                linearColor *= 1.0f / float(sampleCount);

                // convert back to sRGB U8 and write it in the destination pixel
                *destPixel = RGB_F32_To_U8(linearColor);

                // move to the next destination pixel
                ++destPixel;
            }
        }
    }
}

void SaveMips(const ImageMips& texture, const char* fileName)
{
    // figure out the resolution of the composite image
    int width = texture[0].width;
    int height = 0;
    for (const Image& image : texture)
        height += image.height;

    // allocate the destination image
    std::vector<RGBU8> outputImage;
    outputImage.resize(width*height);
    std::fill(outputImage.begin(), outputImage.end(), RGBU8{ 0, 0, 0 });

    // copy the source images row by row
    RGBU8* destPixels = outputImage.data();
    for (const Image& image : texture)
    {
        const RGBU8* srcPixels = image.pixels.data();
        for (int y = 0; y < image.height; ++y)
        {
            memcpy(destPixels, srcPixels, image.width * sizeof(RGBU8));
            destPixels += width;
            srcPixels += image.width;
        }
    }

    // save the combined image
    stbi_write_png(fileName, width, height, 3, outputImage.data(), 0);
}

void TestMipMatrix(const ImageMips& texture, const Matrix22& transform)
{
    int outTextureWidth = texture[0].width * 2;
    int outTextureHeight = texture[0].height * 2;

    std::vector<RGBU8> nearestMip0;
    std::vector<RGBU8> nearestMip;
    std::vector<RGBU8> bilinear;
    std::vector<RGBU8> trilinear;

    nearestMip0.resize(outTextureWidth*outTextureHeight);
    nearestMip.resize(outTextureWidth*outTextureHeight);
    bilinear.resize(outTextureWidth*outTextureHeight);
    trilinear.resize(outTextureWidth*outTextureHeight);

    Vector2 percent;

    // TODO: image size differences are part of mip selection (delta) calculation too!

    // calculate what mip level we are going to be using.
    // It's constant across the whole image because transform is linear.
    Vector2 d_uv_dx = Vector2{ 1.0f, 0.0f } * transform;
    Vector2 d_uv_dy = Vector2{ 0.0f, 1.0f } * transform;
    float lenx = std::sqrtf(Dot(d_uv_dx, d_uv_dx));
    float leny = std::sqrtf(Dot(d_uv_dy, d_uv_dy));
    float maxlen = std::max(lenx, leny);
    float mip = clamp(std::log2f(maxlen), 0.0f, float(texture.size()-1));

    int outputIndex = 0;
    for (int y = 0; y < outTextureHeight; ++y)
    {
        percent[1] = float(y) / float(outTextureHeight);

        for (int x = 0; x < outTextureWidth; ++x)
        {
            percent[0] = float(x) / float(outTextureWidth);

            Vector2 uv = percent * transform;

            // TODO: do a version that samples from mip0 always.
            // TODO: i guess the options would be: no mips nearest, mip nearest, mip bilinear, mip trilinear?

            // TODO: choose nearest mip level by calculating mip and rounding!
            // TODO: wrap uv's? or no?

            // TODO: is it correct to round mip like this?

            // TODO: wrap around is doing something weird on the vertical axis with y scale of 1. look into it!

            nearestMip0[outputIndex] = SampleNearest(texture[0], uv);
            nearestMip[outputIndex] = SampleNearest(texture[int(mip+0.5f)], uv);
            bilinear[outputIndex] = SampleBilinear(texture[0], uv);

            ++outputIndex;
        }
    }

    // TODO: combine the 3 images. vertically? and save the result
    stbi_write_png("out/nearest0.png", outTextureWidth, outTextureHeight, 3, nearestMip0.data(), 0);
    stbi_write_png("out/nearest.png", outTextureWidth, outTextureHeight, 3, nearestMip.data(), 0);
    stbi_write_png("out/bilinear.png", outTextureWidth, outTextureHeight, 3, bilinear.data(), 0);
}

int main(int argc, char **argv)
{
    // Load the scenery image and make mips. Save them out for the blog post too.
    ImageMips texture;
    {
        int width, height, numChannels;
        uint8* image = stbi_load("colors.png", &width, &height, &numChannels, 3); // TODO: scenery.png!
        MakeMips(texture, image, width, height);
        stbi_image_free(image);
    }
    SaveMips(texture, "out/mips.png");

    // test mip scaling
    {
        Matrix22 mat =
        {
            {
                {1.0f, 0.0f},
                {0.0f, 1.0f},
            }
        };

        // TODO: nearest is mirroring with colors.png (?!) and bilinear is all sorts of wrong. check it out.
        TestMipMatrix(texture, mat);
    }

    return 0;
}

/*

* organize the code into a couple files?

? do we go until the longer axis is 1, or the shorter axis?
? do you change mips when it takes 2 pixels, or 1.5 pixels? or > 1 pixels?
? log2(x) is non linear between multiples of 2. Is that correct? I bet so but...

Streth goals:
? do rip maps for aniso?

*/