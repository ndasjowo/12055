/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 */

/*
 * Copyright (C) 2010 The Android Open Source Project
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

#ifndef A_RTP_ASSEMBLER_H_

#define A_RTP_ASSEMBLER_H_

#include <media/stagefright/foundation/ABase.h>
#include <utils/List.h>
#include <utils/RefBase.h>
#ifndef ANDROID_DEFAULT_CODE 
#include <utils/List.h>
#include <media/stagefright/foundation/ABuffer.h>
#endif // #ifndef ANDROID_DEFAULT_CODE

namespace android {

struct ABuffer;
struct ARTPSource;

struct ARTPAssembler : public RefBase {
    enum AssemblyStatus {
        MALFORMED_PACKET,
        WRONG_SEQUENCE_NUMBER,
#ifndef ANDROID_DEFAULT_CODE 
        LARGE_SEQUENCE_GAP,
#endif // #ifndef ANDROID_DEFAULT_CODE
        NOT_ENOUGH_DATA,
        OK
    };

    ARTPAssembler();

#ifndef ANDROID_DEFAULT_CODE 
    static const uint32_t kLargeSequenceGap = 20;
    void onPacketReceived(const sp<ARTPSource> &source, bool flush = false);
#else
    void onPacketReceived(const sp<ARTPSource> &source);
#endif // #ifndef ANDROID_DEFAULT_CODE
    virtual void onByeReceived() = 0;
#ifndef ANDROID_DEFAULT_CODE 
    // do something before time established
    virtual void updatePacketReceived(const sp<ARTPSource> &source, 
            const sp<ABuffer> &buffer);
    virtual void setNextExpectedSeqNo(uint32_t rtpSeq) { return; };
#endif // #ifndef ANDROID_DEFAULT_CODE

protected:
#ifndef ANDROID_DEFAULT_CODE 
    AssemblyStatus getAssembleStatus(List<sp<ABuffer> > *queue, 
            uint32_t nextExpectedSeq) {
        sp<ABuffer> buffer = *--queue->end();
        uint32_t seq = buffer->int32Data();
        return seq - nextExpectedSeq > kLargeSequenceGap ?
            LARGE_SEQUENCE_GAP : WRONG_SEQUENCE_NUMBER;
    }
    // notify ARTPSource to updateExpectedTimeoutUs, mainly for audio
    virtual void evaluateDuration(const sp<ARTPSource> &source, 
            const sp<ABuffer> &buffer) { return; }
#endif // #ifndef ANDROID_DEFAULT_CODE
    virtual AssemblyStatus assembleMore(const sp<ARTPSource> &source) = 0;
    virtual void packetLost() = 0;

    static void CopyTimes(const sp<ABuffer> &to, const sp<ABuffer> &from);

    static sp<ABuffer> MakeADTSCompoundFromAACFrames(
            unsigned profile,
            unsigned samplingFreqIndex,
            unsigned channelConfig,
            const List<sp<ABuffer> > &frames);

    static sp<ABuffer> MakeCompoundFromPackets(
            const List<sp<ABuffer> > &frames);

private:
    int64_t mFirstFailureTimeUs;

    DISALLOW_EVIL_CONSTRUCTORS(ARTPAssembler);
};

}  // namespace android

#endif  // A_RTP_ASSEMBLER_H_
