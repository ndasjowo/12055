/*
 * Copyright (C) 2008 The Android Open Source Project
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

#ifndef AUTODETECT_H
#define AUTODETECT_H

#include <inttypes.h>

// flags used for native encoding detection
enum {
    kEncodingNone               = 0,
    kEncodingShiftJIS           = (1 << 0),
    kEncodingGBK                = (1 << 1),
    kEncodingBig5               = (1 << 2),
    kEncodingEUCKR              = (1 << 3),
#ifndef ANDROID_DEFAULT_CODE
    kEncodingISO8859			= (1 << 4),
    #ifdef VENDOR_EDIT
    //OppoHook
    //property=android.annotation.OppoHook.OppoRomType.OPPO
    //Yugang.Ma@Exp.MultiMedia add for ID3 support of different countries 2013/08/26	
    kEncodingRu			= (1 << 5),
    kEncodingThai			= (1 << 6),
    kEncodingVi			= (1 << 7),	
    #endif /* VENDOR_EDIT */
#endif

    kEncodingAll                = (kEncodingShiftJIS | kEncodingGBK | kEncodingBig5 | kEncodingEUCKR),
};

#ifdef VENDOR_EDIT
//OppoHook
//property=android.annotation.OppoHook.OppoRomType.OPPO
//Yugang.Ma@Exp.MultiMedia add for ID3 support of different countries 2013/08/26
enum {
    kAudioFileNameRegionNone               = 0,
    kAudioFileNameRegionGBK           = (1 << 0),
    kAudioFileNameRegionBig5          = (1 << 1),
    kAudioFileNameRegionThai          = (1 << 2),
    kAudioFileNameRegionRu            = (1 << 3),
    kAudioFileNameRegionVi            = (1 << 4),			
};
#endif /* VENDOR_EDIT */

// returns a bitfield containing the possible native encodings for the given character
extern uint32_t findPossibleEncodings(int ch);

#endif // AUTODETECT_H
