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

int main(int argc, char **argv)
{
    // Load the scenery image and make mips. Save them out for the blog post too.
    ImageMips texture;
    {
        int width, height, numChannels;
        uint8* image = stbi_load("scenery.png", &width, &height, &numChannels, 3);
        MakeMips(texture, image, width, height);
        stbi_image_free(image);
    }
    SaveMips(texture, "out/mips.png");


    return 0;
}

/*

? do we go until the longer axis is 1, or the shorter axis?
? do you change mips when it takes 2 pixels, or 1.5 pixels? or > 1 pixels?

Streth goals:
? do rip maps for aniso?

*/