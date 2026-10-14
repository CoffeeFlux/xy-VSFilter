// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <vector>

#ifdef _MSC_VER
#define XY_DRAWING_TOKENS_CDECL __cdecl
#else
#define XY_DRAWING_TOKENS_CDECL
#endif
extern "C" {
void* XY_DRAWING_TOKENS_CDECL xy_vsfilter_drawing_tokens_create(const uint16_t*, uint32_t);
uint32_t XY_DRAWING_TOKENS_CDECL xy_vsfilter_drawing_tokens_prepare(void*);
void XY_DRAWING_TOKENS_CDECL xy_vsfilter_drawing_tokens_reset(void*,const uint16_t*,uint32_t);
size_t XY_DRAWING_TOKENS_CDECL xy_vsfilter_drawing_tokens_field(const void*, uint32_t, size_t, size_t);
void XY_DRAWING_TOKENS_CDECL xy_vsfilter_drawing_tokens_release(void*);
}
#undef XY_DRAWING_TOKENS_CDECL

namespace XyRust {
class DrawingTokens {
    void* owner;
    size_t cursor;
    DrawingTokens(const DrawingTokens&);
    DrawingTokens& operator=(const DrawingTokens&);
public:
    template<typename String> explicit DrawingTokens(const String& input) : owner(NULL), cursor(0) {
        const int length = input.GetLength();
        if (length < 0) abort();
        std::vector<uint16_t> units(static_cast<size_t>(length));
        for (int i=0; i<length; ++i) units[i] = static_cast<uint16_t>(input[i]);
        owner = xy_vsfilter_drawing_tokens_create(units.data(), static_cast<uint32_t>(length));
        if (!owner) abort();
    }
    ~DrawingTokens() { xy_vsfilter_drawing_tokens_release(owner); }
    bool Prepare(){cursor=0;return xy_vsfilter_drawing_tokens_prepare(owner)!=0;}
    template<class String> void Reset(const String& input){
        std::vector<uint16_t> units(static_cast<size_t>(input.GetLength()));
        for(int i=0;i<input.GetLength();++i)units[i]=static_cast<uint16_t>(input[i]);
        xy_vsfilter_drawing_tokens_reset(owner,units.data(),static_cast<uint32_t>(units.size()));cursor=0;
    }
    template<typename String> bool Next(String& parameters, uint16_t& command) {
        if (cursor >= xy_vsfilter_drawing_tokens_field(owner, 0, 0, 0)) return false;
        command = static_cast<uint16_t>(xy_vsfilter_drawing_tokens_field(owner, 1, cursor, 0));
        const size_t length = xy_vsfilter_drawing_tokens_field(owner, 2, cursor, 0);
        // Each parameter is a substring of the input with nonnegative int length.
        std::vector<wchar_t> units(length + 1, 0);
        for (size_t i=0; i<length; ++i)
            units[i] = static_cast<wchar_t>(xy_vsfilter_drawing_tokens_field(owner, 3, cursor, i));
        parameters = String(units.data(), static_cast<int>(length));
        ++cursor;
        return true;
    }
};
}
