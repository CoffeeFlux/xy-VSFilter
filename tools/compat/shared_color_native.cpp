// SPDX-License-Identifier: GPL-2.0-or-later
// Invoke actual STS helpers and linked RTS parser; never copy their algorithms.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>
#include "legacy_color_names.h"
#include <cerrno>

// Explicit instantiation permits this private member address. The probe calls
// the actual linked method; no class layout/access declaration is rewritten.
struct HtmlMethod {
    typedef bool (CRenderedTextSubtitle::*type)(CSubtitle*,CStringW,STSStyle&,STSStyle&);
    friend type method(HtmlMethod);
};
template<class Tag,typename Tag::type M> struct MethodAccess {
    friend typename Tag::type method(Tag) {return M;}
};
template struct MethodAccess<HtmlMethod,&CRenderedTextSubtitle::ParseHtmlTag>;
static void raw(const CStringW& value) {
    printf("%d:",value.GetLength());
    for(int i=0;i<value.GetLength();++i)printf("%04x",static_cast<unsigned>(static_cast<uint16_t>(value[i])));
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE,"AfxWinInit failed");
    printf("colors-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());
    std::vector<CStringW> values;
    const wchar_t* literals[]={L"",L"0",L"00",L"000000",L"-0",L"+0",L"1",L"-1",L"+1",L"ff",L"ffffff",L"00112233",L"7fffffff",L"80000000",L"ffffffff",L"-80000000",L"-80000001",L"999999999999999999",L"0x0",L"0x123456",L"#0",L"##0",L"#black",L"##red",L"red",L"RED",L"black",L"Black",L"none",L"transparent",L"  red",L"red ",L"\tff",L"ffjunk",L"+",L"-",L"#",L"##",L"0xfftail",L"12345678"};
    for(auto text:literals)values.push_back(CStringW(text));
    for(auto name:legacy_color_names){CStringW text(name);values.push_back(text);values.push_back(L"#"+text);values.push_back(L"##"+text);text.MakeUpper();values.push_back(text);}
    for(unsigned u=0;u<256;++u){WCHAR a[]={L'f',static_cast<WCHAR>(u),L'1',L'2'},b[]={L'r',L'e',L'd',0,static_cast<WCHAR>(u)};values.push_back(CStringW(a,4));values.push_back(CStringW(b,5));}
    const WCHAR extras[][8]={{L'b',L'l',L'a',L'c',L'k',0,L'f',L'f'},{0,L'r',L'e',L'd'},{L'#',0,L'r',L'e',L'd'},{0xd800,L'f',L'f'},{L'f',L'f',0xdfff},{0x2003,L'f',L'f'},{L'1',0,L'f'},{L'#',L'#',0,L'f'}};
    for(const auto& a:extras)values.push_back(CStringW(a,8));
    check(values.size()==1112,"color fixture count");
    CCritSec lock;CRenderedTextSubtitle renderer(&lock);unsigned index=0;
    for(unsigned v=0;v<values.size();++v)for(unsigned kind=0;kind<8;++kind){
        const CStringW input=values[v];CStringW output;bool accepted=true;STSStyle current,original;
        for(int c=0;c<4;++c){current.colors[c]=0x112233+c;current.alpha[c]=11+c;original.colors[c]=0x8899aa+c;}
        current.fontSize=23;original.fontSize=29;
        CStringW tag=L"font ";const wchar_t* attribute=kind==5?L"color":kind==6?L"outline-color":L"shadow-color";
        tag+=attribute;tag+=L"=\"";tag+=input;tag+=L"\"";
        CStringW sami=L"<font color=\"";sami+=input;sami+=L"\">caption</font>";
        errno=1234;
        if(kind<3)output=SSAColorTag(input,kind==0?L"c":kind==1?L"3c":L"");
        else if(kind==3)output=CStringW(SSAColorTagCS(std::wstring(input.GetString(),input.GetLength())).c_str());
        else if(kind==4)output=SMI2SSA(sami,ANSI_CHARSET);
        else accepted=(renderer.*method(HtmlMethod{}))(nullptr,tag,current,original);
        const int error=errno;
        printf("colors-case index=%u value=%u kind=%u errno=%d outcome=%u input=",index++,v,kind,error,accepted?1:0);raw(input);printf(" output=");raw(output);printf("\n");
        if(kind>=5)style(current);
    }
    printf("colors-complete cases=%u values=%zu\n",index,values.size());return 0;
}catch(const std::exception& error){fprintf(stderr,"unresolved: %s\n",error.what());return 1;}catch(...){fprintf(stderr,"unresolved exception\n");return 2;}}
