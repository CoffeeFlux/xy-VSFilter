// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ssf_wide.h"
static uint64_t histories=0,reads=0,fields=0,aliases=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
}
static void effects(std::vector<int64_t>& out){const int error=errno,flags=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef _WIN32
    const unsigned long last=GetLastError();
#else
    const unsigned long last=0;
#endif
    out.insert(out.end(),{error,flags,last});
}
template<class Stream>static std::vector<int64_t> execute(const std::vector<WCHAR>& input,bool count){
    WCHAR empty=0;CStringW source(input.empty()?&empty:input.data(),static_cast<int>(input.size()));
    const WCHAR* alias=source.GetString();std::vector<int64_t> result;seed();
    {
        Stream stream(source);effects(result);
        result.insert(result.end(),{stream.m_pos,stream.m_str.GetLength(),stream.m_encoding,source.GetString()==alias});
        for(size_t i=0;i<input.size();++i){
            source.SetAt(static_cast<int>(i),static_cast<WCHAR>(input[i]^0x5a5a));
            // The stream retains the original CString reference, so a caller's
            // pointer to those units stays live through source COW mutation.
            result.push_back(alias[i]);if(count)++aliases;
        }
        result.push_back(source.GetString()==alias);
        for(size_t step=0;step<input.size()+4;++step){
            seed();int value=stream.NextByte();effects(result);
            result.insert(result.end(),{value,stream.m_pos,stream.m_str.GetLength(),stream.m_encoding,stream.m_line,stream.m_col});
            if(count)++reads;
        }
        seed();
    }
    effects(result);
    for(int i=0;i<source.GetLength();++i)result.push_back(source[i]);
    return result;
}
static void compare(const std::vector<WCHAR>& input,bool decline){
    ssfWideDecline=decline;
    const auto a=execute<ReferenceSsf::WCharInputStream>(input,true),b=execute<CandidateSsf::WCharInputStream>(input,false);
    if(a!=b){fprintf(stderr,"SSF wide mismatch history=%llu length=%zu decline=%d\n",(unsigned long long)histories,input.size(),decline);std::abort();}
    ++histories;fields+=a.size();
}
int main(){
    for(bool decline:{false,true}){
        compare({},decline);
        for(unsigned unit=0;unit<=0xffff;++unit)compare({static_cast<WCHAR>(unit),0,0xd800,0xffff,static_cast<WCHAR>(unit)},decline);
        std::vector<WCHAR> input;for(unsigned unit=0;unit<=0xffff;++unit)input.push_back(static_cast<WCHAR>(unit));compare(input,decline);
        for(unsigned length:{1U,2U,3U,15U,16U,17U,127U,128U,129U,1023U}){
            input.clear();for(unsigned i=0;i<length;++i)input.push_back(static_cast<WCHAR>((i*73)^0xd800));compare(input,decline);
        }
    }
    ssfRequire(histories&&reads&&aliases&&ssfWideOwners&&ssfWideOwners==ssfWideReleases&&ssfWideReads&&ssfWideDeclines);
    printf("%llu exact SSF wide-input histories / %llu NextByte calls / %llu code-unit, string, cursor and ambient-error fields passed (%llu retained source-alias units after COW mutation / %llu Rust owners and releases / %llu Rust reads / %llu modeled Rust declines; whole pinned WCharInputStream constructor and NextByte, every UTF-16 unit, embedded NUL and unpaired surrogates, complete long input, source mutation and old-pointer lifetime, empty strings and repeated EOF; actual Windows CStringW or portable COW string model; native CString anchor retained, allocation failure and invalid string/index states excluded)\n",(unsigned long long)histories,(unsigned long long)reads,(unsigned long long)fields,(unsigned long long)aliases,(unsigned long long)ssfWideOwners,(unsigned long long)ssfWideReads,(unsigned long long)ssfWideDeclines);
}
