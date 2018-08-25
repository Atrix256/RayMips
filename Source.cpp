#define _CRT_SECURE_NO_WARNINGS

#include "MatrixMath.h"
#include "Images.h"
#include "Math.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

void SaveCombinedImages2x2(const char* fileName, int width, int height, const RGBU8* p00, const RGBU8* p10, const RGBU8* p01, const RGBU8* p11)
{
    std::vector<RGBU8> output;
    output.resize((width * 2 + 1) * (height * 2 + 1));

    for (int y = 0; y < height; ++y)
    {
        RGBU8* dest00 = &output[y * (width * 2 + 1)];
        RGBU8* dest10 = &output[y * (width * 2 + 1) + width + 1];
        RGBU8* dest01 = &output[(y + height + 1) * (width * 2 + 1)];
        RGBU8* dest11 = &output[(y + height + 1) * (width * 2 + 1) + width + 1];

        int sourceIndex = y * width;

        for (int x = 0; x < width; ++x)
        {
            dest00[0] = p00[sourceIndex];
            dest10[0] = p10[sourceIndex];
            dest01[0] = p01[sourceIndex];
            dest11[0] = p11[sourceIndex];

            ++dest00;
            ++dest10;
            ++dest01;
            ++dest11;
            ++sourceIndex;
        }
    }

    stbi_write_png(fileName, width * 2 + 1, height * 2 + 1, 3, output.data(), 0);
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

void TestMipMatrix(const ImageMips& texture, const Matrix33& uvtransform, int width, int height, const char* fileName)
{
    // TODO: multiplication order? Should matter with rotation.

    // account for the image scale in this transform
    float imageScaleX = float(texture[0].width) / float(width);
    float imageScaleY = float(texture[0].height) / float(height);
    Matrix33 imageScale = Scale33({ imageScaleX, imageScaleY, 1.0f });

    Matrix33 derivativesTransform = imageScale * uvtransform;

    std::vector<RGBU8> nearestMip0;
    std::vector<RGBU8> nearestMip;
    std::vector<RGBU8> bilinear;
    std::vector<RGBU8> trilinear;

    nearestMip0.resize(width*height);
    nearestMip.resize(width*height);
    bilinear.resize(width*height);
    trilinear.resize(width*height);



    // calculate what mip level we are going to be using.
    // It's constant across the whole image because transform is linear.
    Vector3 d_uv_dx_3 = Vector3{ 1.0f, 0.0f, 0.0f } * derivativesTransform;
    Vector3 d_uv_dy_3 = Vector3{ 0.0f, 1.0f, 0.0f } * derivativesTransform;
    Vector2 d_uv_dx = { d_uv_dx_3[0], d_uv_dx_3[1] };
    Vector2 d_uv_dy = { d_uv_dy_3[0], d_uv_dy_3[1] };
    float lenx = std::sqrtf(Dot(d_uv_dx, d_uv_dx));
    float leny = std::sqrtf(Dot(d_uv_dy, d_uv_dy));
    float maxlen = std::max(lenx, leny);
    float mip = clamp(std::log2f(maxlen), 0.0f, float(texture.size()-1));
    int mipInt = clamp(int(mip), 0, int(texture.size() - 1));  // you could also add 0.5 before casting to int to round it. I felt that looked too blurry

    Vector3 percent = { 0.0f, 0.0f, 1.0f };
    int outputIndex = 0;
    for (int y = 0; y < height; ++y)
    {
        percent[1] = PixelToUV(y, height);

        for (int x = 0; x < width; ++x)
        {
            percent[0] = PixelToUV(x, width);

            Vector3 uv3 = percent * uvtransform;
            Vector2 uv = { uv3[0], uv3[1] };

            nearestMip0[outputIndex] = SampleNearest(texture[0], uv);
            nearestMip[outputIndex] = SampleNearest(texture[mipInt], uv);
            bilinear[outputIndex] = SampleBilinear(texture[mipInt], uv);
            trilinear[outputIndex] = SampleTrilinear(texture, uv, mip);

            ++outputIndex;
        }
    }

    SaveCombinedImages2x2(fileName, width, height, nearestMip0.data(), nearestMip.data(), bilinear.data(), trilinear.data());
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
        Matrix33 mat = Scale33({ 3.0f, 1.0f, 1.0f });

        TestMipMatrix(texture, mat, texture[0].width, texture[0].height,"out/scale.png");
    }

    // test rotation
    {
        Matrix33 mat = Rotation33(DegreesToRadians(90.0f));
        TestMipMatrix(texture, mat, texture[0].width, texture[0].height, "out/rot90.png");

        mat = Rotation33(DegreesToRadians(20.0f));
        TestMipMatrix(texture, mat, texture[0].width, texture[0].height, "out/rot20.png");

        mat = Rotation33(DegreesToRadians(20.0f));
        TestMipMatrix(texture, mat, texture[0].width*2, texture[0].height*2, "out/rot20large.png");

        // TODO: figure out how to make sure the multiplication order is correct inside TestMipMatrix
    }

    // test mip translation
    {
        Matrix33 mat = Translate33({0.2f, 0.2f});
        TestMipMatrix(texture, mat, texture[0].width, texture[0].height,"out/translation.png");
    }

    // TODO: srgb correction on load and save?

    return 0;
}

/*
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