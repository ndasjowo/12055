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

#ifndef AMR_NB_DECODER_H_

#define AMR_NB_DECODER_H_

#include <media/oppostagefright/MediaSource.h>

namespace android {

struct MediaBufferGroup;

struct AMRNBDecoder : public MediaSource {
    AMRNBDecoder(const sp<MediaSource> &source);

    virtual status_t start(MetaData *params);
    virtual status_t stop();

    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options);

protected:
    virtual ~AMRNBDecoder();

private:
    sp<MediaSource> mSource;
    bool mStarted;

    MediaBufferGroup *mBufferGroup;

    void *mState;
    int64_t mAnchorTimeUs;
    int64_t mNumSamplesOutput;

    MediaBuffer *mInputBuffer;

    AMRNBDecoder(const AMRNBDecoder &);
    AMRNBDecoder &operator=(const AMRNBDecoder &);
};

}  // namespace android

#endif  // AMR_NB_DECODER_H_
