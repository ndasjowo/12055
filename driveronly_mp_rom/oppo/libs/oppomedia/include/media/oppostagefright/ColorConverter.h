/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef COLOR_CONVERTER_H_

#define COLOR_CONVERTER_H_

#include <sys/types.h>

#include <stdint.h>
#include <utils/Errors.h>

#include <OMX_Video.h>

namespace android {

struct ColorConverter {
    ColorConverter(OMX_COLOR_FORMATTYPE from, OMX_COLOR_FORMATTYPE to);
    ~ColorConverter();

    bool isValid() const;

    status_t convert(
            const void *srcBits,
            size_t srcWidth, size_t srcHeight,
            size_t srcCropLeft, size_t srcCropTop,
            size_t srcCropRight, size_t srcCropBottom,
            void *dstBits,
            size_t dstWidth, size_t dstHeight,
            size_t dstCropLeft, size_t dstCropTop,
            size_t dstCropRight, size_t dstCropBottom);

private:
    struct BitmapParams {
        BitmapParams(
                void *bits,
                size_t width, size_t height,
                size_t cropLeft, size_t cropTop,
                size_t cropRight, size_t cropBottom);

        size_t cropWidth() const;
        size_t cropHeight() const;

        void *mBits;
        size_t mWidth, mHeight;
        size_t mCropLeft, mCropTop, mCropRight, mCropBottom;
    };

    OMX_COLOR_FORMATTYPE mSrcFormat, mDstFormat;
    uint8_t *mClip;

    uint8_t *initClip();

    status_t convertCbYCrY(
            const BitmapParams &src, const BitmapParams &dst);

    status_t convertYUV420Planar(
            const BitmapParams &src, const BitmapParams &dst);

    status_t convertQCOMYUV420SemiPlanar(
            const BitmapParams &src, const BitmapParams &dst);

    status_t convertYUV420SemiPlanar(
            const BitmapParams &src, const BitmapParams &dst);

    status_t convertTIYUV420PackedSemiPlanar(
            const BitmapParams &src, const BitmapParams &dst);

/////////////////////////////////////////////qcom
enum ColorConvertFormat {
    RGB565 = 1,
    YCbCr420Tile,
    YCbCr420SP,
    YCbCr420P,
    YCrCb420P,
};

/* 64 bit flag variable, reserving bits as needed */
enum ColorConvertFlags {
    COLOR_CONVERT_ALIGN_NONE = 1,
    COLOR_CONVERT_CENTER_OUTPUT = 1<<1,
    COLOR_CONVERT_ALIGN_16 =   1<<4,
    COLOR_CONVERT_ALIGN_2048 = 1<<11,
    COLOR_CONVERT_ALIGN_8192 = 1<<13,
};

struct ColorConvertParams {
    size_t width;
    size_t height;

    size_t cropWidth;
    size_t cropHeight;

    size_t cropLeft;
    size_t cropRight;
    size_t cropTop;
    size_t cropBottom;

    ColorConvertFormat colorFormat;
    const void * data;
    int fd;

    uint64_t flags;
};
#define OMX_QCOM_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka  0x7FA30C03
typedef int (* ConvertFn)(ColorConvertParams src,
                          ColorConvertParams dst, uint8_t *adjustedClip);
///////////////////////////////////////////////////////////////

    ColorConverter(const ColorConverter &);
    ColorConverter &operator=(const ColorConverter &);
};

}  // namespace android

#endif  // COLOR_CONVERTER_H_
