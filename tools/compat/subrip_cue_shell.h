// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sami_file_shell.h"
#include "xy_rust_subrip_cue.h"
#include "xy_rust_subrip.h"
#undef swscanf_s
#ifndef _countof
#define _countof(value) (sizeof(value)/sizeof(*(value)))
#endif
static uint64_t cueOwners=0,cueEmpty=0,cueBoundaries=0,cueAppends=0;
namespace XyRust {
class ObservedSubRipCue : public SubRipCue {
public:
    ObservedSubRipCue(){++cueOwners;}
    void EmptyLine(){++cueEmpty;SubRipCue::EmptyLine();}
    bool Boundary(bool numeric)const{++cueBoundaries;return SubRipCue::Boundary(numeric);}
    template<class String> void Append(const String& fragment){++cueAppends;SubRipCue::Append(fragment);}
};
}
// Windows executes the original secure CRT formats. Portable builds use the
// corresponding wide-character scanf spellings and discard secure size args.
static int cueScan(const wchar_t* text,const wchar_t* fmt,int* h1,wchar_t* a,unsigned na,int* m1,wchar_t* b,unsigned nb,int* s1,wchar_t* ms1,unsigned n1,int* h2,wchar_t* c,unsigned nc,int* m2,wchar_t* d,unsigned nd,int* s2,wchar_t* ms2,unsigned n2){
    microObserve(51,text);
#ifdef _WIN32
    int result=::swscanf_s(text,fmt,h1,a,na,m1,b,nb,s1,ms1,n1,h2,c,nc,m2,d,nd,s2,ms2,n2);
#else
    (void)fmt;(void)na;(void)nb;(void)n1;(void)nc;(void)nd;(void)n2;
    int result=std::swscanf(text,L"%d%lc%d%lc%d%4l[^-] --> %d%lc%d%lc%d%4ls\n",h1,a,m1,b,s1,ms1,h2,c,m2,d,s2,ms2);
#endif
    searchEvents.push_back(static_cast<uint32_t>(result));
    const int* numbers[]={h1,m1,s1,h2,m2,s2};const int counts[]={1,3,5,7,9,11};
    for(int i=0;i<6;++i)if(result>=counts[i])searchEvents.push_back(static_cast<uint32_t>(*numbers[i]));
    if(result>=2)searchEvents.push_back(static_cast<uint16_t>(*a));
    for(unsigned i=0;i<n1;++i)searchEvents.push_back(static_cast<uint16_t>(ms1[i]));
    for(unsigned i=0;i<n2;++i)searchEvents.push_back(static_cast<uint16_t>(ms2[i]));
    searchEvents.push_back(errno);return result;
}
static int cueScan(const wchar_t* text,const wchar_t* fmt,wchar_t* sep,unsigned count,int* number){
    microObserve(52,text);
#ifdef _WIN32
    int result=::swscanf_s(text,fmt,sep,count,number);
#else
    (void)fmt;(void)count;int result=std::swscanf(text,L"%lc%d",sep,number);
#endif
    searchEvents.push_back(static_cast<uint32_t>(result));
    if(result>=1)searchEvents.push_back(static_cast<uint16_t>(*sep));
    if(result>=2)searchEvents.push_back(static_cast<uint32_t>(*number));
    searchEvents.push_back(errno);return result;
}
static int cueScan(const wchar_t* text,const wchar_t* fmt,int* number,wchar_t* sep,unsigned count){
    microObserve(53,text);
#ifdef _WIN32
    int result=::swscanf_s(text,fmt,number,sep,count);
#else
    (void)fmt;(void)count;int result=std::swscanf(text,L"%d%lc",number,sep);
#endif
    searchEvents.push_back(static_cast<uint32_t>(result));
    if(result>=1)searchEvents.push_back(static_cast<uint32_t>(*number));
    if(result>=2)searchEvents.push_back(static_cast<uint16_t>(*sep));
    searchEvents.push_back(errno);return result;
}
#define swscanf_s cueScan
