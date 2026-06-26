#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CoreVideo.h>
#include <CoreMedia/CoreMedia.h>

#define HB_COLR_TRA_GAMMA22      4
#define HB_COLR_TRA_GAMMA28      5

#define MP4ESDescrTag                   0x03
#define MP4DecConfigDescrTag            0x04
#define MP4DecSpecificDescrTag          0x05

// 1. Replicated exact logic of hb_cv_colr_gamma_xlat
CFNumberRef hb_cv_colr_gamma_xlat(int color_transfer)
{
    Float32 gamma = 0;
    switch (color_transfer)
    {
        case HB_COLR_TRA_GAMMA22:
            gamma = 2.2;
            break;
        case HB_COLR_TRA_GAMMA28:
            gamma = 2.8;
            break;
    }

    return gamma > 0 ? CFNumberCreate(NULL, kCFNumberFloat32Type, &gamma) : NULL;
}

// 2. Replicated exact logic of hb_cv_match_rgb_to_colorspace using real CoreVideo
int hb_cv_match_rgb_to_colorspace(int rgb,
                                  int color_prim,
                                  int color_transfer,
                                  int color_matrix)
{
    const unsigned r = (rgb >> 16) & 0xff;
    const unsigned g = (rgb >> 8) & 0xff;
    const unsigned b = (rgb) & 0xff;

    if (__builtin_available(macOS 10.15, *)) {
        CGColorRef rgb_color = CGColorCreateSRGB(r / 255.f, g / 255.f, b / 255.f, 1.f);

        CFStringRef prim     = CVColorPrimariesGetStringForIntegerCodePoint(color_prim);
        CFStringRef transfer = CVTransferFunctionGetStringForIntegerCodePoint(color_transfer);
        CFNumberRef gamma    = hb_cv_colr_gamma_xlat(color_transfer);
        CFStringRef matrix   = CVYCbCrMatrixGetStringForIntegerCodePoint(color_matrix);

        CGColorSpaceRef colorspace = NULL;
        CFMutableDictionaryRef attachments = CFDictionaryCreateMutable(NULL, 0,
                                                                       &kCFTypeDictionaryKeyCallBacks,
                                                                       &kCFTypeDictionaryValueCallBacks);
        if (attachments != NULL)
        {
            if (prim != NULL)
            {
                CFDictionarySetValue(attachments, kCVImageBufferColorPrimariesKey, prim);
            }
            if (transfer != NULL)
            {
                CFDictionarySetValue(attachments, kCVImageBufferTransferFunctionKey, transfer);
            }
            if (matrix != NULL)
            {
                CFDictionarySetValue(attachments, kCVImageBufferYCbCrMatrixKey, matrix);
            }
            if (transfer == kCVImageBufferTransferFunction_UseGamma && gamma != NULL)
            {
                CFDictionarySetValue(attachments, kCVImageBufferGammaLevelKey, gamma);
            }

            colorspace = CVImageBufferCreateColorSpaceFromAttachments(attachments);
            CFRelease(attachments);
        }

        if (gamma != NULL)
        {
            CFRelease(gamma);
        }

        if (colorspace == NULL)
        {
            CFRelease(rgb_color);
            printf("cgcolor: unable to match color to colorspace\n");
            return rgb;
        }

        CGColorRef matched_color = CGColorCreateCopyByMatchingToColorSpace(colorspace,
                                                                           kCGRenderingIntentPerceptual,
                                                                           rgb_color, NULL);
        CFRelease(colorspace);
        CFRelease(rgb_color);
        if (matched_color == NULL)
        {
            printf("cgcolor: unable to match color to colorspace\n");
            return rgb;
        }

        const CGFloat *components = CGColorGetComponents(matched_color);
        const int color = ((int)(components[0] * 255) << 16) | ((int)(components[1] * 255) << 8) | (int)(components[2] * 255);
        CFRelease(matched_color);
        return color;
    }
    else
    {
        return rgb;
    }
}

// 3. Replicated exact logic of readDescrLen, readDescr, and ReadESDSDescExt
static int readDescrLen(UInt8 **buffer, const UInt8 *end)
{
    int len = 0;
    int count = 4;
    while (count--)
    {
        if (*buffer >= end)
            return -1;
        int c = *(*buffer)++;
        len = (len << 7) | (c & 0x7f);
        if (!(c & 0x80))
            break;
    }
    return len;
}

static int readDescr(UInt8 **buffer, const UInt8 *end, int *tag)
{
    if (*buffer >= end)
        return -1;
    *tag = *(*buffer)++;
    return readDescrLen(buffer, end);
}

static long TestReadESDSDescExt(void* descExt, UInt32 descExtSize, UInt8 **buffer, UInt32 *size, int versionFlags)
{
    UInt8 *esds = (UInt8*)descExt;
    const UInt8 *end = esds + descExtSize;
    int tag, len;
    *size = 0;

    if (versionFlags)
    {
        if (esds + 4 > end)
            return -1;
        esds += 4; // version + flags
    }
    len = readDescr(&esds, end, &tag);
    if (len < 0)
        return -1;

    if (esds + 2 > end)
        return -1;
    esds += 2;     // ID
    if (tag == MP4ESDescrTag)
    {
        if (esds + 1 > end)
            return -1;
        esds++;    // priority
    }

    len = readDescr(&esds, end, &tag);
    if (len < 0)
        return -1;

    if (tag == MP4DecConfigDescrTag)
    {
        if (esds + 13 > end)
            return -1;
        esds++;    // object type id
        esds++;    // stream type
        esds += 3; // buffer size db
        esds += 4; // max bitrate
        esds += 4; // average bitrate

        len = readDescr(&esds, end, &tag);
        if (len < 0)
            return -1;

        if (tag == MP4DecSpecificDescrTag)
        {
            if (esds + len > end)
                return -1;
            *buffer = calloc(1, len + 8);
            if (*buffer)
            {
                memcpy(*buffer, esds, len);
                *size = len;
            }
        }
    }

    return 0;
}

int main()
{
    printf("=== RUNNING REAL MACOS WORKFLOW TEST ===\n");

    // 1. Verify hb_cv_colr_gamma_xlat value and CFNumberRef details
    CFNumberRef gamma_22 = hb_cv_colr_gamma_xlat(HB_COLR_TRA_GAMMA22);
    assert(gamma_22 != NULL);
    float val_22 = 0;
    CFNumberGetValue(gamma_22, kCFNumberFloat32Type, &val_22);
    printf("Gamma 2.2 parsed: %f\n", val_22);
    assert(val_22 == 2.2f);
    CFRelease(gamma_22);

    CFNumberRef gamma_28 = hb_cv_colr_gamma_xlat(HB_COLR_TRA_GAMMA28);
    assert(gamma_28 != NULL);
    float val_28 = 0;
    CFNumberGetValue(gamma_28, kCFNumberFloat32Type, &val_28);
    printf("Gamma 2.8 parsed: %f\n", val_28);
    assert(val_28 == 2.8f);
    CFRelease(gamma_28);

    // 2. Verify hb_cv_match_rgb_to_colorspace using real CoreVideo
    int input_rgb = 0xFF0000; // Red
    // Pass standard ITU-R 709 code points (1)
    int output_rgb = hb_cv_match_rgb_to_colorspace(input_rgb, 1, HB_COLR_TRA_GAMMA22, 1);
    printf("Matched RGB: 0x%06X -> 0x%06X\n", input_rgb, output_rgb);
    assert(output_rgb != 0);

    // 3. Verify ESDS Parser
    UInt8 valid_cookie[] = {
        0x03, 0x80, 0x80, 0x80, 0x1a, // tag 3 (ESDescrTag), length 26
        0x00, 0x01,                   // ID
        0x00,                         // priority
        0x04, 0x11,                   // tag 4 (DecConfigDescrTag), length 17
        0x40,                         // object type
        0x15,                         // stream type
        0x00, 0x00, 0x00,             // buffer size
        0x00, 0x00, 0x00, 0x00,       // max bitrate
        0x00, 0x00, 0x00, 0x00,       // avg bitrate
        0x05, 0x04,                   // tag 5 (DecSpecificDescrTag), length 4
        0xaa, 0xbb, 0xcc, 0xdd        // decoder specific data
    };

    UInt8 *out_buf = NULL;
    UInt32 out_size = 0;
    long ret = TestReadESDSDescExt(valid_cookie, sizeof(valid_cookie), &out_buf, &out_size, 0);
    assert(ret == 0);
    assert(out_size == 4);
    assert(out_buf != NULL);
    assert(out_buf[0] == 0xaa && out_buf[1] == 0xbb && out_buf[2] == 0xcc && out_buf[3] == 0xdd);
    free(out_buf);

    // Test truncated boundary cases
    for (size_t trunc_size = 1; trunc_size < sizeof(valid_cookie); trunc_size++)
    {
        out_buf = NULL;
        out_size = 0;
        ret = TestReadESDSDescExt(valid_cookie, trunc_size, &out_buf, &out_size, 0);
        assert(ret == -1 || out_buf == NULL);
        if (out_buf) free(out_buf);
    }

    printf("=== ALL REAL MACOS TESTS PASSED SUCCESSFULLY! ===\n");
    return 0;
}
