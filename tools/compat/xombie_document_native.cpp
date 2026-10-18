// SPDX-License-Identifier: GPL-2.0-or-later
// Include the selected checkout's actual parser, rather than a rewritten oracle.
// Reuse the initialized document observer; its standalone main is not invoked.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
// Observe the exact process-private font installation requests at the system
// boundary. Rendering comparisons separately exercise the real Windows call.
static unsigned font_calls=0;
static HANDLE WINAPI TraceAddFontMemResourceEx(PVOID data,DWORD size,PVOID reserved,DWORD* count) {
    if(reserved || !count || !data || !size) throw std::runtime_error("bad font installation request");
    printf("xombie-font index=%u bytes=%lu data=",font_calls++,static_cast<unsigned long>(size));
    const BYTE* bytes=static_cast<const BYTE*>(data);
    for(DWORD i=0;i<size;++i)printf("%02x",bytes[i]);
    printf("\n");
    *count=(font_calls&1)?0:1;
    return (font_calls&1)?NULL:reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(1));
}
#define AddFontMemResourceEx TraceAddFontMemResourceEx
#include <STS.cpp>
#undef AddFontMemResourceEx

static void xombie_case(unsigned index,const CStringW& input,bool preload) {
    TraceDocument document(0);document.Screen(CSize(0,0));
    if(preload)document.Add(L"existing",true,0,10000000,L"Default",L"",L"",CRect(0,0,0,0),0);
    const CStringW encoded=CStringW(L"\xfeff")+input;
    CTextFile file;
    if(!file.OpenMemory(static_cast<LPCWSTR>(encoded),encoded.GetLength()*sizeof(WCHAR)))throw std::runtime_error("fixture open failed");
    int outcome;
    try {outcome=OpenXombieSub(&file,document,ANSI_CHARSET)?1:0;}
    catch(int value){outcome=-100-value;}
    printf("xombie-case index=%u outcome=%d position=%llu\n",index,outcome,file.GetPosition());
    document.Dump("xombie-result");
}
int main() {
 try {
    if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
    printf("xombie-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());
    unsigned index=0;
    auto emit=[&](const CStringW& text){for(int preload=0;preload<2;++preload)xombie_case(index++,text,preload!=0);};
    for(auto key:{L"Version",L"ScreenHorizontal",L"ScreenVertical"})for(auto value:{L"",L"junk",L"0",L"-7",L"1280",L"1024",L"2147483647",L"-2147483648",L"&Hffffffff",L"nan",L"1.25junk"})for(int newline=0;newline<2;++newline){
        CStringW text(key);text+=L"=";text+=value;if(newline)text+=L"\r\n";emit(text);
    }
    const wchar_t* style[]={L"First",L"Second",L"Arial",L"20",L"4294967295",L"2",L"3",L"4",L"5",L"6",L"7",L"8",L"-1",L"0",L"0",L"0",L"0",L"100",L"100",L"0",L"1",L"2",L"3",L"1",L"2",L"3",L"2",L"10",L"20",L"30",L"1"};
    const wchar_t* variations[]={L"",L"junk",L"-1",L"-0",L"nan",L"inf",L"1.5",L"2147483647",L"&Hffffffff",L"\xd800"};
    for(int field=0;field<31;++field)for(auto value:variations){
        CStringW text(L"Style=");for(int i=0;i<31;++i){if(i)text+=L",";text+=i==field?value:style[i];}text+=L"\r\n";emit(text);
    }
    const wchar_t* line[]={L"D",L"id",L"9",L"0:00:01.234",L"0:00:02.345",L"First",L"Second",L"Actor",L"1",L"2",L"3",L"  text  "};
    for(int field=0;field<12;++field)for(auto value:variations){
        CStringW text(L"Line=");for(int i=0;i<12;++i){if(i)text+=L",";text+=i==field?value:line[i];}text+=L"\r\n";emit(text);
    }
    CStringW valid(L"Style=");for(int i=0;i<31;++i){if(i)valid+=L",";valid+=style[i];}
    valid+=L"\r\nLine=D,id,9,0:00:01.234,0:00:02.345,First,Second,Actor,1,2,3,text\r\n";emit(valid);
    for(int tail=0;tail<20;++tail)emit(valid.Left(valid.GetLength()-tail));
    const wchar_t units[]={0,9,13,32,0xa0,0x2003,0xd800,0xffff};
    for(auto unit:units)for(int pos=0;pos<12;++pos){CStringW raw=valid;raw.SetAt(pos*3,unit);emit(raw);}
    for(auto text:{L"",L" \t",L";comment",L"unknown=ignored\r\n",L"fontname=first\r\n!!\r\nfontname:second\r\n!!!\r\n[Events]\r\n",L"fontname=first\r\n!!",L"Line=X,ignored\r\n",L"Line=D\r\n"})emit(CStringW(text));
    printf("xombie-font-complete calls=%u\n",font_calls);
    printf("xombie-complete cases=%u\n",index);return 0;
 }catch(const std::exception& error){fprintf(stderr,"unresolved: %s\n",error.what());return 1;}
 catch(...){fprintf(stderr,"unresolved exception\n");return 2;}
}
