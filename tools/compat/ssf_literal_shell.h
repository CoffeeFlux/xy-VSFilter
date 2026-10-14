// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#include <windows.h>
#include <atlbase.h>
#include <atlstr.h>
#else
typedef char16_t WCHAR;
#endif
#ifdef _T
#undef _T
#endif
#define _T(x) x
struct LiteralInterrupted {int kind;explicit LiteralInterrupted(int k):kind(k){}};
struct LiteralContext {
    std::vector<int> input;
    size_t position=0;
    int fail=-1,attempt=0;
    bool after=false,skew=false;
    std::vector<int64_t> events;
};
static LiteralContext* literalContext=NULL;
static int literalBegin(int operation){
    const int attempt=literalContext->attempt++;
    literalContext->events.insert(literalContext->events.end(),{operation,attempt,static_cast<int64_t>(literalContext->position)});
    if(attempt==literalContext->fail&&!literalContext->after)throw LiteralInterrupted(1);
    return attempt;
}
static void literalEnd(int attempt){if(attempt==literalContext->fail&&literalContext->after)throw LiteralInterrupted(2);}
class LiteralString {
#ifdef _WIN32
    CStringW value;
#else
    std::u16string value;
#endif
public:
    void operator+=(WCHAR c){
        const int attempt=literalBegin(4);
        literalContext->events.push_back(c);
        value+=c;
        literalEnd(attempt);
    }
    std::vector<int64_t> Units()const{
        std::vector<int64_t> result;
#ifdef _WIN32
        for(int i=0;i<value.GetLength();++i)result.push_back(value[i]);
#else
        for(const auto c:value)result.push_back(c);
#endif
        return result;
    }
};
struct Stream {enum {EOS=-1};};
struct InputStream:Stream {
    int Current()const{return literalContext->position<literalContext->input.size()?literalContext->input[literalContext->position]:EOS;}
    int SkipWhiteSpace(const wchar_t* more=NULL){
        const int attempt=literalBegin(more?1:0);
        int c=Current();
        while(c==32||c==9||c==10||c==13||(more&&(c==':'||c=='='))){++literalContext->position;c=Current();}
        literalContext->events.push_back(c);literalEnd(attempt);return c;
    }
    int PeekChar(){const int attempt=literalBegin(3);const int c=Current();literalContext->events.push_back(c);literalEnd(attempt);return c;}
    int GetChar(){
        const int attempt=literalBegin(2);int c=Current();
        if(literalContext->position<literalContext->input.size())++literalContext->position;
        // Controlled provider allows Get to differ from prior Peek, including
        // a transient EOS. Native methods must still choose the same error.
        if(literalContext->skew&&literalContext->position==2)c=EOS;
        literalContext->events.push_back(c);literalEnd(attempt);return c;
    }
    void ThrowError(const char* message){
        const int attempt=literalBegin(7);
        for(const char* p=message;*p;++p)literalContext->events.push_back(*p);
        literalContext->events.push_back(0);literalEnd(attempt);throw LiteralInterrupted(3);
    }
};
struct Definition {
    enum {string=1,block=2};
    int status=17;
    std::vector<int64_t> value={70,0,71};
    void SetAsValue(int kind,const LiteralString& text){
        const int attempt=literalBegin(5);const auto units=text.Units();
        literalContext->events.insert(literalContext->events.end(),{kind,static_cast<int64_t>(units.size())});
        literalContext->events.insert(literalContext->events.end(),units.begin(),units.end());
        status=kind;value=units;literalEnd(attempt);
    }
};
#include "xy_rust_ssf_literal.h"
static uint64_t literalSteps=0;
namespace XyRust {
class ObservedSsfLiteral:public SsfLiteral {
public:
    explicit ObservedSsfLiteral(bool block):SsfLiteral(block){}
    uint32_t Next(int last){++literalSteps;return SsfLiteral::Next(last);}
};
}
