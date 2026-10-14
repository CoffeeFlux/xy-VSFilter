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
class CStringW {
    std::u16string value;
public:
    CStringW(){}
    CStringW(const WCHAR* input,int length):value(input,input+length){}
    void operator+=(WCHAR c){value+=c;}
    int GetLength()const{return static_cast<int>(value.size());}
    WCHAR operator[](int i)const{return value.at(i);}
};
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
    int textDecline=-2,textAttempt=0;
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
    CStringW value;
public:
    void operator+=(WCHAR c){
        const int attempt=literalBegin(4);
        literalContext->events.push_back(c);
        value+=c;
        literalEnd(attempt);
    }
    std::vector<int64_t> Units()const{
        std::vector<int64_t> result;
        for(int i=0;i<value.GetLength();++i)result.push_back(value[i]);
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
    template<class T>void SetAsValue(int kind,const T& text){
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

#include "xy_rust_ssf_literal_text.h"
static uint64_t literalTextOwners=0,literalTextReleases=0,literalTextAppends=0,literalTextDeclines=0,literalTextCopies=0;
static void* literalTextCreate(){
    if(literalContext->textDecline==-1){++literalTextDeclines;return NULL;}
    void* result=xy_vsfilter_ssf_literal_text_create();if(result)++literalTextOwners;return result;
}
static void literalTextRelease(void* owner){if(owner)++literalTextReleases;xy_vsfilter_ssf_literal_text_release(owner);}
static uint32_t literalTextAppend(void* owner,uint16_t value){
    if(literalContext->textAttempt++==literalContext->textDecline){++literalTextDeclines;return 0;}
    ++literalTextAppends;return xy_vsfilter_ssf_literal_text_append(owner,value);
}
static const uint16_t* literalTextData(const void* owner){++literalTextCopies;return xy_vsfilter_ssf_literal_text_data(owner);}
template<class Storage>class LiteralStringRust {
    Storage value;
public:
    void operator+=(WCHAR c){const int attempt=literalBegin(4);literalContext->events.push_back(c);value+=c;literalEnd(attempt);}
    std::vector<int64_t> Units()const{
        const CStringW copy=static_cast<CStringW>(value);std::vector<int64_t> result;
        for(int i=0;i<copy.GetLength();++i)result.push_back(copy[i]);return result;
    }
};
