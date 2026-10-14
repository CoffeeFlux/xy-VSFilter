// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "ssf_stream_shell.h"
struct Exception:public SsfInterrupted {
    explicit Exception(const wchar_t* message):SsfInterrupted{3}{
        ssfContext->events.push_back(3);for(const wchar_t* p=message;*p;++p)ssfContext->events.push_back(*p);
    }
};
static uint64_t ssfBomSteps=0,ssfUtf16Steps=0;
static int32_t ssfObservedUtf16(int32_t first,int32_t second){++ssfUtf16Steps;return xy_vsfilter_ssf_utf16(first,second);}
namespace XyRust {
class ObservedSsfBom:public SsfBom {public:uint32_t Next(int byte){++ssfBomSteps;return SsfBom::Next(byte);}};
}
