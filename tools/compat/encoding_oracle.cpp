// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_encoding.h"
static uint64_t comparisons=0,changed=0,units=0,callbacks=0,interruptions=0,failedConversions=0,trailingLeads=0;
static std::vector<uint16_t> snapshot(const CStringW& str){
    std::vector<uint16_t> result;
    for(int i=0;i<str.GetLength();++i)result.push_back(static_cast<uint16_t>(str[i]));
    return result;
}
static CStringW invoke(bool candidate,unsigned function,const CStringW& str,int charset){
    if(candidate){switch(function){
    case 0:return CandidateEncoding::ToMBCS(str,charset);
    case 1:return CandidateEncoding::ToUnicode(str,charset);
    case 2:return CandidateEncoding::UnicodeSSAToMBCS(str,charset);
    default:return CandidateEncoding::MBCSSSAToUnicode(str,charset);
    }}
    switch(function){
    case 0:return ReferenceEncoding::ToMBCS(str,charset);
    case 1:return ReferenceEncoding::ToUnicode(str,charset);
    case 2:return ReferenceEncoding::UnicodeSSAToMBCS(str,charset);
    default:return ReferenceEncoding::MBCSSSAToUnicode(str,charset);
    }
}
static void compare(const std::wstring& input,unsigned function,int charset,int mode=0,int leadMode=0,int failure=-1){
    microCase=comparisons;microInput=input.data();microLength=static_cast<int>(input.size());microSide=0;
    CStringW str(input.data(),static_cast<int>(input.size())),a,b;
    encodingMode=mode;searchMode=leadMode;searchFailure=failure;searchAttempts=0;searchEvents.clear();
    bool interruptedA=false,interruptedB=false;errno=EDOM;
#ifdef _WIN32
    ::SetLastError(0x51);
#endif
    try{a=invoke(false,function,str,charset);}catch(const SearchInterrupted&){interruptedA=true;}
    const int errnoA=errno;
#ifdef _WIN32
    const DWORD errorA=::GetLastError();
#endif
    auto events=searchEvents;searchAttempts=0;searchEvents.clear();microSide=1;errno=EDOM;
#ifdef _WIN32
    ::SetLastError(0x51);
#endif
    try{b=invoke(true,function,str,charset);}catch(const SearchInterrupted&){interruptedB=true;}
    const int errnoB=errno;
#ifdef _WIN32
    if(errorA!=::GetLastError()){fprintf(stderr,"LastError mismatch function=%u charset=%d errors=%lu/%lu\n",function,charset,errorA,::GetLastError());std::abort();}
#endif
    if(snapshot(a)!=snapshot(b) || interruptedA!=interruptedB || errnoA!=errnoB || events!=searchEvents){
        fprintf(stderr,"Encoding mismatch function=%u charset=%d mode=%d leadMode=%d failure=%d lengths=%d/%d callbacks=%zu/%zu errno=%d/%d\n",function,charset,mode,leadMode,failure,a.GetLength(),b.GetLength(),events.size(),searchEvents.size(),errnoA,errnoB);std::abort();
    }
    ++comparisons;changed+=snapshot(a)!=snapshot(str);units+=a.GetLength();callbacks+=events.size();interruptions+=interruptedA;
    if(mode==1 && !input.empty() && !interruptedA)++failedConversions;
    if(function==1 && input.size()==1 && input[0]==0x81 && charset==128 && !interruptedA)++trailingLeads;
}
int main(){
    microDiagnostics();
    const int charsets[]={0,1,2,128,129,130,134,136,255,-1,65536};
    const wchar_t* fragments[]={L"",L"ASCII",L"{\\fe128}x{\\fe}y",L"{\\fe134}x",L"{\\fe}x",L"{\\fe-1}x",L"{\\FE128}x",L"{\\fe2147483647}x",L"{\\fe128",L"{x{\\fe136}y}",L"{{}}",L"}x{",L"{\\fe128\\fe0}x",L"{\\fe12x34}z"};
    for(const auto* fragment:fragments)for(int charset:charsets)for(unsigned function=0;function<4;++function)
        for(int mode=0;mode<3;++mode)for(int lead=0;lead<3;++lead)compare(fragment,function,charset,mode,lead);
    // Every raw UTF-16 code unit, with NUL and following text; every byte pair
    // across the Windows DBCS charsets, including invalid/trailing combinations.
    for(unsigned unit=0;unit<=0xffff;++unit){
        std::wstring single(1,static_cast<wchar_t>(unit));
        for(unsigned function=0;function<2;++function)for(int mode=0;mode<3;++mode)compare(single,function,128,mode);
        std::wstring mixed=L"a";mixed+=single;mixed.push_back(0);mixed+=L"z";
        compare(mixed,0,0);compare(mixed,1,128);
        std::wstring tagged=L"a{\\fe128}";tagged+=single;tagged.push_back(0);tagged+=L"tail{\\fe}z";
        compare(tagged,2,0);compare(tagged,3,128);
        std::wstring pair;pair.push_back(static_cast<wchar_t>(unit>>8));pair.push_back(static_cast<wchar_t>(unit&255));
        for(int charset:{128,129,130,134,136})compare(pair,1,charset);
    }
    for(int charset=0;charset<256;++charset)for(unsigned byte=0;byte<256;++byte){
        std::wstring s(1,static_cast<wchar_t>(0xff00|byte));s+=L"x";
        compare(s,0,charset);compare(s,1,charset);
    }
    uint32_t random=0xe09134f1;
    for(unsigned n=0;n<12000;++n){std::wstring s;
        for(unsigned i=0;i<n%13;++i){random^=random<<13;random^=random>>17;random^=random<<5;
            s+=fragments[random%14];s.push_back(static_cast<wchar_t>(random&65535));}
        // Whole SSA wrappers include embedded NULs and charset transitions.
        for(unsigned function=2;function<4;++function)compare(s,function,charsets[n%11],n%3,n%3);
    }
    const std::wstring interrupted=L"text{\\fe128}\x81\x40{\\fe}tail";
    for(unsigned function=0;function<4;++function)for(int mode=0;mode<3;++mode)for(int failure=0;failure<90;++failure)
        compare(interrupted,function,128,mode,0,failure);
    if(!changed || !interruptions || !encodingOwners || !encodingByteReplies || !encodingUnitReplies || !failedConversions || !trailingLeads || !ssaEncodingOwners || !ssaEncodingSteps || !ssaEncodingAppends)return 1;
    printf("%llu exact encoding comparisons passed (%llu changed strings, %llu output UTF-16 units, %llu callback observation values, %llu injected interruptions, %llu failed-conversion fixtures, %llu trailing-lead controls; %llu Rust owners / %llu input reads / %llu byte replies / %llu unit replies; %llu SSA owners / %llu steps / %llu appended fragments; complete ToMBCS, ToUnicode and SSA wrappers, all UTF-16 values, all DBCS byte pairs, all charset/byte pairs, embedded NULs, ordered native APIs and errno/Windows LastError; portable conversion models are not Windows code-page evidence)\n",(unsigned long long)comparisons,(unsigned long long)changed,(unsigned long long)units,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)failedConversions,(unsigned long long)trailingLeads,(unsigned long long)encodingOwners,(unsigned long long)encodingTakes,(unsigned long long)encodingByteReplies,(unsigned long long)encodingUnitReplies,(unsigned long long)ssaEncodingOwners,(unsigned long long)ssaEncodingSteps,(unsigned long long)ssaEncodingAppends);
}
