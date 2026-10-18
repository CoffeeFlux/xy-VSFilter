// SPDX-License-Identifier: GPL-2.0-or-later
// Actual selected OpenUSF/CUSFSubtitles and initialized document observer.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>
#include <USFSubtitles.cpp>

static void usf_case(unsigned index,const CStringW& input,bool preload,unsigned kind=0) {
    TraceDocument document(0);document.Screen(CSize(0,0));
    if(preload)document.Add(L"existing",true,0,10000000,L"Default",L"",L"",CRect(0,0,0,0),0);
    const CStringW encoded=CStringW(L"\xfeff")+input;
    const auto data=reinterpret_cast<const BYTE*>(static_cast<LPCWSTR>(encoded));
    const size_t size=encoded.GetLength()*sizeof(WCHAR);
    CTextFile file;check(file.OpenMemory(data,size),"USF fixture open failed");
    bool accepted=false;
    if(kind==0)accepted=OpenUSF(&file,document,ANSI_CHARSET);
    else {
        CUSFSubtitles parser;accepted=parser.Read(data,size);
        if(accepted){
            check(parser.ConvertToSTS(document),"USF first conversion failed");
            document.Dump("usf-first-conversion");
            if(kind==2){
                const BYTE invalid[]={0xff,0xfe,'<',0,'x',0};
                check(!parser.Read(invalid,sizeof invalid),"USF invalid reload accepted");
            }
            check(parser.ConvertToSTS(document),"USF repeated conversion failed");
        }
    }
    printf("usf-case index=%u outcome=%u position=%llu\n",index,accepted?1:0,file.GetPosition());
    document.Dump("usf-result");
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE,"MFC initialization failed");
    check(SUCCEEDED(CoInitializeEx(NULL,COINIT_APARTMENTTHREADED)),"COM initialization failed");
    printf("usf-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());
    unsigned index=0;
    auto emit=[&](const CStringW& text,unsigned kind=0){for(int preload=0;preload<2;++preload)usf_case(index++,text,preload!=0,kind);};
    const wchar_t* roots[]={L"USFSubtitles",L"usfsubtitles",L"USFSUBTITLES",L"wrong",L"USFSubtitles version='1.0'"};
    const wchar_t* bodies[]={L"",L"<subtitles/>",L"<subtitles><subtitle start='1' stop='2'><text>A</text></subtitle></subtitles>",L"<subtitles><subtitle start='1' duration='2'><text>A</text></subtitle></subtitles>",L"<subtitles><subtitle stop='2'><text>A</text></subtitle></subtitles>",L"<subtitles><subtitle start='1'><text>A</text></subtitle></subtitles>"};
    for(auto root:roots)for(auto body:bodies){CStringW name(root);int space=name.Find(' ');if(space>=0)name=name.Left(space);CStringW text;text.Format(L"<%s>%s</%s>\r\n",root,body,static_cast<LPCWSTR>(name));emit(text);}
    const wchar_t* times[]={L"",L"0",L"1",L"-1",L"1.2",L"1:2",L"1:2.3",L"1:2:3.4",L"1::2",L"abc",L"1x",L"2147483647",L"2147483648",L"4294967296",L"99999999999999999",L".5",L"1,2",L"1:2:3:4:5",L" 2 ",L"-1:-2.-3"};
    for(auto start:times)for(auto stop:times){CStringW text;text.Format(L"<USFSubtitles><subtitles><subtitle start='%s' stop='%s' duration='3'><text>A</text></subtitle></subtitles></USFSubtitles>\r\n",start,stop);emit(text);}
    const wchar_t* fonts[]={L"",L"face='Arial'",L"size='0'",L"size='-1'",L"size='20'",L"weight='normal'",L"weight='bold'",L"weight='lighter'",L"weight='bolder'",L"weight='0'",L"italic='yes' underline='YES'",L"color='#80112233' alpha='50'",L"color='red' back-color='blue' outline-color='#ff00ff' shadow-color='bad'",L"alpha='-1' outline-level='2' shadow-level='3'",L"wrap='no'",L"wrap='auto'",L"wrap='bad'",L"color='##123456' alpha='300'"};
    const wchar_t* positions[]={L"",L"alignment='TopLeft'",L"alignment='bad'",L"relative-to='video'",L"horizontal-margin='10%' vertical-margin='20'",L"horizontal-margin='-5%' vertical-margin='2147483647%'",L"rotate-z='10' rotate-x='-2' rotate-y='3'",L"relative-to='WINDOW' alignment='middlecenter'"};
    for(auto font:fonts)for(auto position:positions){CStringW text;text.Format(L"<USFSubtitles><styles><style name='Default'><fontstyle %s/><position %s/></style></styles><subtitles><subtitle start='1' stop='5'><text %s>A<b>B</b><i>C</i></text></subtitle></subtitles></USFSubtitles>\r\n",font,position,position);emit(text);}
    const wchar_t* inheritance[]={L"",L"<style name='Default'><fontstyle size='20' wrap='no'/></style>",L"<style name='default'><fontstyle size='20' wrap='no'/></style><style name='Default'><fontstyle size='25' wrap='auto'/></style>",L"<style name='Named'><fontstyle color='red'/></style><style name='Default'><fontstyle size='20' wrap='no'/></style>",L"<style name='Named'><fontstyle color='red'/><fontstyle size='25'/></style>",L"<wrapper><style name='Nested'><fontstyle size='25'/></style></wrapper>",L"<style name='Named'><position alignment='TopLeft'/><position rotate-z='30'/></style>",L"<style name='Named'><fontstyle wrap='no'/></style><style name='Named'><fontstyle wrap='auto'/></style>"};
    for(auto styles:inheritance){CStringW text;text.Format(L"<USFSubtitles><metadata><language code='en'>English</language><languageext code='fr'>French</languageext></metadata><styles>%s</styles><subtitles><subtitle start='1' stop='3'><text style='Named' alignment='TopLeft' rotate-z='30'>A</text><text style='named'>B</text><text>C</text></subtitle></subtitles></USFSubtitles>\r\n",styles);for(unsigned kind=0;kind<3;++kind)emit(text,kind);}
    const wchar_t* captions[]={L"A &amp; B &lt; C",L"  A\r\n B  ",L"<b>A<i>B</i>C</b>",L"<font face='Arial' size='20' color='#ff112233' outline-level='2' shadow-level='1'>A</font>",L"<k t='25'>ignored</k>A<br>ignored</br>B",L"<unknown>A</unknown><!--comment--><![CDATA[B]]>",L"\u0130\uff11\u2003",L"<font color='red'><font color='blue'>B</font>R</font>"};
    for(auto caption:captions){CStringW text;text.Format(L"<USFSubtitles><subtitles><subtitle start='1' stop='5'><text>%s</text><karaoke>%s</karaoke></subtitle></subtitles></USFSubtitles>\r\n",caption,caption);emit(text);}
    // 5*6*2 + 20*20*2 + 18*8*2 + 8*3*2 + 8*2 = 1212.
    printf("usf-complete cases=%u\n",index);CoUninitialize();return 0;
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 1;}catch(...){fprintf(stderr,"unresolved exception\n");return 2;}}
