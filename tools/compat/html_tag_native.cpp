// SPDX-License-Identifier: GPL-2.0-or-later
// Actual linked renderer method and initialized state; no copied parser.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <cerrno>
struct HtmlMethod {typedef bool (CRenderedTextSubtitle::*type)(CSubtitle*,CStringW,STSStyle&,STSStyle&);friend type member(HtmlMethod);};
struct KType {typedef int CRenderedTextSubtitle::*type;friend type member(KType);};
struct KStart {typedef int CRenderedTextSubtitle::*type;friend type member(KStart);};
struct KEnd {typedef int CRenderedTextSubtitle::*type;friend type member(KEnd);};
template<class Tag,typename Tag::type M> struct Access {friend typename Tag::type member(Tag){return M;}};
// Explicit instantiations obtain actual private member addresses without
// modifying access declarations, layout, or implementation in either checkout.
template struct Access<HtmlMethod,&CRenderedTextSubtitle::ParseHtmlTag>;
template struct Access<KType,&CRenderedTextSubtitle::m_ktype>;
template struct Access<KStart,&CRenderedTextSubtitle::m_kstart>;
template struct Access<KEnd,&CRenderedTextSubtitle::m_kend>;
static void raw(const CStringW& value){printf("%d:",value.GetLength());for(int i=0;i<value.GetLength();++i)printf("%04x",static_cast<unsigned>(static_cast<uint16_t>(value[i])));}
static void init(CRenderedTextSubtitle& renderer,STSStyle& current,STSStyle& original,unsigned seed){
 current=STSStyle();original=STSStyle();current.fontName=L"CurrentFace";original.fontName=L"OriginalFace";
 const uint64_t sizes[]={0x4037800000000000ull,0x8000000000000000ull,0x7ff0000000000000ull,0x7ff8123456789abcull};
 memcpy(&current.fontSize,&sizes[seed],8);original.fontSize=31.25;
 current.fontWeight=321;original.fontWeight=543;current.fItalic=false;original.fItalic=true;current.fUnderline=true;original.fUnderline=false;current.fStrikeOut=false;original.fStrikeOut=true;
 for(int c=0;c<4;++c){current.colors[c]=0x112233+c;original.colors[c]=0x8899aa+c;current.alpha[c]=11+c;original.alpha[c]=21+c;}
 current.outlineWidthX=2.5;current.outlineWidthY=3.5;current.shadowDepthX=4.5;current.shadowDepthY=5.5;
 renderer.*member(KType{})=7+seed;renderer.*member(KStart{})=10-seed;
 const int ends[]={-9,2147483646,-2147483647,123};renderer.*member(KEnd{})=ends[seed];
}
static void run(CRenderedTextSubtitle& renderer,STSStyle& current,STSStyle& original,const CStringW& input,unsigned index,unsigned seed,unsigned group){
 errno=1234;const bool accepted=(renderer.*member(HtmlMethod{}))(nullptr,input,current,original);const int error=errno;
 printf("html-case index=%u seed=%u group=%u accepted=%u errno=%d ktype=%d kstart=%d kend=%d input=",index,seed,group,accepted?1:0,error,renderer.*member(KType{}),renderer.*member(KStart{}),renderer.*member(KEnd{}));raw(input);printf("\n");
 style(current);style(original);
}
static const wchar_t* literals[]={L"",L" ",L"/",L" /",L" /b",L"/ b",L" b ",L"!--",L"!-- comment",L"!---->",L"text",L"TEXT",L"b",L"strong",L"/strong",L"i",L"em",L"/em",L"u",L"/u",L"s",L"strike",L"del",L"/del",L"font",L"/font",L"/font color=red",L"unknown",L"k",L"k t=10",L"k t=-2",L"k t=2147483647",L"k t=1 x=2",L"k t=1 t=2",L"k T=\"0x10\"",L"font =x",L"font face=",L"font face=\"",L"font face=\"Arial Times\" size=\"-2\"",L"font size=+2 size=-3 color=black color=0",L"font size='12'",L" font size=12/",L"k t=",L"font\tcolor=red"};
static const wchar_t* attributes[]={L"face",L"size",L"color",L"outline-color",L"outline-level",L"shadow-color",L"shadow-level"};
static const wchar_t* values[]={L"",L"0",L"-0",L"+0",L"1",L"-2",L"+3",L"2147483647",L"2147483648",L"-2147483649",L"99999999999",L"Arial Times",L"RED",L"##black",L"ff",L"\"",L" ",L"\t",L"<",L"&",L"0x12",L"1 x=2"};
static const wchar_t* sequence_tags[]={L"b",L"font face=Arial size=-2 color=red",L"k t=2147483647",L"k t=2",L"k t=-3",L"font outline-level=5 shadow-level=-2",L"/font",L"/b",L"unknown",L"i",L"/i",L"k t=0"};

int main(){try{
 check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE,"AfxWinInit failed");
 printf("html-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());
 std::vector<CStringW> inputs;for(auto text:literals)inputs.push_back(CStringW(text));
 for(auto a:attributes)for(auto v:values)for(bool quoted:{false,true}){CStringW text=L"font ";text+=a;text+=L"=";if(quoted)text+=L"\"";text+=v;if(quoted)text+=L"\"";inputs.push_back(text);}
 auto inject=[&](unsigned u){const wchar_t* texts[]={L"font face=AbC size=-2",L"font size=\"12\" color=red",L"k t=12",L"font color=red"};const int positions[]={11,12,4,0};for(int i=0;i<4;++i){CStringW text(texts[i]);text.SetAt(positions[i],static_cast<WCHAR>(u));inputs.push_back(text);}};
 for(unsigned u=0;u<256;++u)inject(u);for(unsigned u:{0x85,0xa0,0x2003,0x130,0xd800,0xdfff,0xfeff,0xffff})inject(u);
 check(inputs.size()==1408,"HTML fixture count");CCritSec lock;CRenderedTextSubtitle renderer(&lock);unsigned index=0;
 for(unsigned seed=0;seed<4;++seed)for(const auto& text:inputs){STSStyle current,original;init(renderer,current,original,seed);run(renderer,current,original,text,index++,seed,0);}
 for(unsigned seed=0;seed<4;++seed){STSStyle current,original;init(renderer,current,original,seed);for(int repeat=0;repeat<2;++repeat)for(auto text:sequence_tags)run(renderer,current,original,CStringW(text),index++,seed,1);}
 printf("html-complete cases=%u inputs=%zu\n",index,inputs.size());return 0;
}catch(const std::exception& error){fprintf(stderr,"unresolved: %s\n",error.what());return 1;}catch(...){fprintf(stderr,"unresolved exception\n");return 2;}}
