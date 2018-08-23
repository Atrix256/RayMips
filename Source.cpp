#define _CRT_SECURE_NO_WARNINGS

#include "MatrixMath.h"
#include "Images.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <vector>
#include <algorithm>

static const float c_pi = 3.14159265359f;

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
    int x = UVToPixel(uv[0], image.width) % image.width;
    int y = UVToPixel(uv[1], image.height) % image.height;

    return image.pixels[y*image.width + x];
}

RGBU8 SampleBilinear(const Image& image, const Vector2& uv)
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

RGBU8 SampleTrilinear(const ImageMips& texture, const Vector2& uv, float mip)
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

void TestMipMatrix(const ImageMips& texture, const Matrix22& uvtransform, int width, int height, const char* baseFileName)
{
    // TODO: multiplication order? Should matter with rotation.

    // account for the image scale in this transform
    float imageScaleX = float(texture[0].width) / float(width);
    float imageScaleY = float(texture[0].height) / float(height);
    Matrix22 imageScale =
    {
        {
            {imageScaleX, 0.0f},
            {0.0f, imageScaleY}
        }
    };
    Matrix22 derivativesTransform = imageScale * uvtransform;

    std::vector<RGBU8> nearestMip0;
    std::vector<RGBU8> nearestMip;
    std::vector<RGBU8> bilinear;
    std::vector<RGBU8> trilinear;

    nearestMip0.resize(width*height);
    nearestMip.resize(width*height);
    bilinear.resize(width*height);
    trilinear.resize(width*height);

    Vector2 percent;

    // calculate what mip level we are going to be using.
    // It's constant across the whole image because transform is linear.
    Vector2 d_uv_dx = Vector2{ 1.0f, 0.0f } * derivativesTransform;
    Vector2 d_uv_dy = Vector2{ 0.0f, 1.0f } * derivativesTransform;
    float lenx = std::sqrtf(Dot(d_uv_dx, d_uv_dx));
    float leny = std::sqrtf(Dot(d_uv_dy, d_uv_dy));
    float maxlen = std::max(lenx, leny);
    float mip = clamp(std::log2f(maxlen), 0.0f, float(texture.size()-1));
    int mipInt = clamp(int(mip), 0, int(texture.size() - 1));  // you could also add 0.5 before casting to int to round it. I felt that looked too blurry

    int outputIndex = 0;
    for (int y = 0; y < height; ++y)
    {
        percent[1] = PixelToUV(y, height);

        for (int x = 0; x < width; ++x)
        {
            percent[0] = PixelToUV(x, width);

            Vector2 uv = percent * uvtransform;

            nearestMip0[outputIndex] = SampleNearest(texture[0], uv);
            nearestMip[outputIndex] = SampleNearest(texture[mipInt], uv);
            bilinear[outputIndex] = SampleBilinear(texture[mipInt], uv);
            trilinear[outputIndex] = SampleTrilinear(texture, uv, mip);

            ++outputIndex;
        }
    }

    // TODO: combine the images and save the result? or leave them separate?
    char fileName[256];

    sprintf(fileName, "%s_a_none.png", baseFileName);
    stbi_write_png(fileName, width, height, 3, nearestMip0.data(), 0);

    sprintf(fileName, "%s_b_mip.png", baseFileName);
    stbi_write_png(fileName, width, height, 3, nearestMip.data(), 0);

    sprintf(fileName, "%s_c_bilinear.png", baseFileName);
    stbi_write_png(fileName, width, height, 3, bilinear.data(), 0);

    sprintf(fileName, "%s_d_trilinear.png", baseFileName);
    stbi_write_png(fileName, width, height, 3, trilinear.data(), 0);
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

    // test mip scaling
    {
        Matrix22 mat =
        {
            {
                {3.0f, 0.0f},
                {0.0f, 1.0f},
            }
        };

        TestMipMatrix(texture, mat, texture[0].width, texture[0].height,"out/scale");
    }

    // test rotation
    {
        // TODO: maybe make a degrees to radians macro and use it here!
        // TODO: test a 90 degree rotation, and a less clean rotation too
        Matrix22 mat = Rotation22(c_pi*0.5f);
        TestMipMatrix(texture, mat, texture[0].width, texture[0].height, "out/rot90");

        mat = Rotation22(c_pi*0.1111f);
        TestMipMatrix(texture, mat, texture[0].width, texture[0].height, "out/rot20");

        mat = Rotation22(c_pi*0.1111f);
        TestMipMatrix(texture, mat, texture[0].width*2, texture[0].height*2, "out/rot20large");

        // TODO: combine with a scaled image to make sure multiplication order etc is correct!
        // TODO: figure out how to make sure the multiplication order is correct.
    }

    return 0;
}

/*

* organize the code into a couple files?
* todos
* maybe do 3x3 matrices where the 3rd row is for translation

? do we go until the longer axis is 1, or the shorter axis?
? do you change mips when it takes 2 pixels, or 1.5 pixels? or > 1 pixels?
? log2(x) is non linear between multiples of 2. Is that correct? I bet so but...

Streth goals:
? do rip maps for aniso?

* Blog:
 ? should triangle filtering be mentioned to go along with box filtering? or nah?
 * show show nearest0, nearest, bilinear, trilinear look. maybe slowly animate gif of a still?
  * maybe show some 4 way split screen animated gif of texture scrolling, zooming, rotating?
  * mip level selection links
   * https://www.opengl.org/discussion_boards/showthread.php/177520-Mipmap-level-calculation-using-dFdx-dFdy
   * https://amp.reddit.com/r/opengl/comments/3cdg5r/derivation_of_opengls_mipmap_level_computation/
   * rounding mip chosen looked too blurry to me so i went with floor(mip) for nearest and bilinear
  * translation
  * uniform scaling
  * non uniform scaling (and show how max of pixel dimension choosing mip affects things!)
   * talk about rip maps and anisotropic sampling
   * https://en.wikipedia.org/wiki/Anisotropic_filtering
  * rotation

*/