// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "microdvd_shell.h"
#include "xy_rust_encoding.h"
#include <cerrno>
static unsigned encodingMode=0;
static uint64_t encodingOwners=0,encodingTakes=0,encodingByteReplies=0,encodingUnitReplies=0;
static uint64_t ssaEncodingOwners=0,ssaEncodingSteps=0,ssaEncodingAppends=0;
namespace XyRust {
class ObservedSsaEncoding : public SsaEncoding {
public:
    template<class String> explicit ObservedSsaEncoding(const String& input):SsaEncoding(input){++ssaEncodingOwners;}
    uint32_t Step(int reply){++ssaEncodingSteps;return SsaEncoding::Step(reply);}
    template<class String> void Append(const String& input){++ssaEncodingAppends;SsaEncoding::Append(input);}
};
class ObservedEncoding : public Encoding {
public:
    template<class String> explicit ObservedEncoding(const String& input):Encoding(input){++encodingOwners;}
    int Take(){++encodingTakes;return Encoding::Take();}
    void AppendBytes(const char* p,int n){++encodingByteReplies;Encoding::AppendBytes(p,n);}
    void AppendUnit(uint16_t unit){++encodingUnitReplies;Encoding::AppendUnit(unit);}
};
}
static int observedWide(DWORD cp,DWORD flags,const WCHAR* input,int count,char* output,int capacity,const char* fallback,int* used){
    searchObserve(20,cp,flags);
    searchEvents.push_back(static_cast<uint32_t>(count));searchEvents.push_back(static_cast<uint32_t>(capacity));
    for(int i=0;i<count;++i)searchEvents.push_back(static_cast<uint16_t>(input[i]));
    if(count!=1 || capacity!=8 || fallback || used)std::abort();
    int result=0;
    if(encodingMode==1){result=0;}
    else if(encodingMode==2){
        // Defined callback result covering every legal output length, including
        // zero bytes and high-bit values. Only returned bytes are initialized.
        result=1+(static_cast<unsigned>(input[0])%8);
        for(int i=0;i<result;++i)output[i]=static_cast<char>((input[0]+i*127)&255);
    }else{
#ifdef _WIN32
        result=::WideCharToMultiByte(cp,flags,input,count,output,capacity,fallback,used);
#else
        // Portable deterministic model, not a Windows code-page implementation.
        if(input[0]>=0xd800 && input[0]<=0xdfff)result=0;
        else if(input[0]<256){output[0]=static_cast<char>(input[0]);result=1;}
        else{output[0]=static_cast<char>(input[0]>>8);output[1]=static_cast<char>(input[0]);result=2;}
#endif
    }
    searchEvents.push_back(static_cast<uint32_t>(result));
    for(int i=0;i<result;++i)searchEvents.push_back(static_cast<unsigned char>(output[i]));
    return result;
}
static int observedMulti(DWORD cp,DWORD flags,const char* input,int count,WCHAR* output,int capacity){
    searchObserve(21,cp,flags);
    searchEvents.push_back(static_cast<uint32_t>(count));searchEvents.push_back(static_cast<uint32_t>(capacity));
    searchEvents.push_back(static_cast<uint16_t>(*output));
    for(int i=0;i<count;++i)searchEvents.push_back(static_cast<unsigned char>(input[i]));
    if((count!=1 && count!=2) || capacity!=1)std::abort();
    int result=0;
    if(encodingMode==1){result=0;}
    else if(encodingMode==2){*output=0;result=0;}
    else{
#ifdef _WIN32
        result=::MultiByteToWideChar(cp,flags,input,count,output,capacity);
#else
        if(count==1){*output=static_cast<unsigned char>(input[0]);result=1;}
        else if(static_cast<unsigned char>(input[1])>=0x40){
            *output=static_cast<WCHAR>((static_cast<unsigned char>(input[0])<<8)|static_cast<unsigned char>(input[1]));result=1;
        }
#endif
    }
    searchEvents.push_back(static_cast<uint32_t>(result));searchEvents.push_back(static_cast<uint16_t>(*output));
    return result;
}
#define WideCharToMultiByte observedWide
#define MultiByteToWideChar observedMulti
