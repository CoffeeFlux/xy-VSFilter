// SPDX-License-Identifier: GPL-2.0-or-later
// Include the selected checkout's actual parser, rather than a rewritten oracle.
// Reuse the initialized document observer; its standalone main is not invoked.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>

static void ass_case(unsigned index, const CStringW& input, CSize screen) {
    TraceDocument document(0);
    document.Screen(screen);
    const CStringW encoded = CStringW(L"\xfeff") + input;
    CTextFile file;
    if(!file.OpenMemory(static_cast<LPCWSTR>(encoded), encoded.GetLength()*sizeof(WCHAR)))
        throw std::runtime_error("memory fixture open failed");
    int outcome;
    try {outcome = OpenSubStationAlpha(&file, document, ANSI_CHARSET) ? 1 : 0;}
    catch(int value) {outcome = -100-value;}
    // All other exceptions, crashes, and timeouts are unresolved runner failures.
    printf("ass-case index=%u outcome=%d position=%llu\n",index,outcome,file.GetPosition());
    document.Dump("ass-result");
}

int main() {
    try {
        if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))
            throw std::runtime_error("AfxWinInit failed");
        printf("ass-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());
        unsigned index=0;
        const wchar_t* keys[]={L"PlayResX",L"PlayResY",L"LayoutResX",L"LayoutResY",L"WrapStyle"};
        const wchar_t* numbers[]={L"0",L"-7",L"1280",L"1024",L"2147483647",L"-2147483648",L"&H80000000",L"0x7fffffff",L"1junk",L"",L"junk",L",tail"};
        for(auto key:keys) for(auto number:numbers) for(int live=0;live<2;++live) {
            CStringW text(L"[Script Info]\r\n");text+=key;text+=L": ";text+=number;text+=L"\r\n";
            text+=L"Collisions: Reverse\r\n"; // must not execute after numeric failure
            ass_case(index++,text,live?CSize(640,480):CSize(0,0));
        }
        const wchar_t* texts[]={
            L";comment\r\nUnknown: ignored\r\n",
            L"[Script Info]",L"[Events]",L"[v4 styles]",L"[v4+ styles]",L"[v4++ styles]",
            L"[script info]\r\nPlayResX: 1280\r\nPlayResY: 0\r\nPlayResX: -7\r\n",
            L"[script info]\r\nPlayResY: 1024\r\nPlayResX: 0\r\nPlayResY: -7\r\n",
            L"[script info]\r\nCollisions: unreversed\r\nScaledBorderAndShadow: notyes\r\n",
            L"[script info]\r\nCollisions: normal\r\nScaledBorderAndShadow: no\r\n",
            L"[script info]\r\nCollisions:\r\n",L"[script info]\r\nScaledBorderAndShadow:\r\n",
            L"[script info]\r\nYCbCr Matrix:\r\n",L"[script info]\r\nYCbCr Matrix: none\r\n",
            L"[script info]\r\nYCbCr Matrix: tv.601junk\r\n",L"[script info]\r\nYCbCr Matrix: tv.709\r\n",
            L"[script info]\r\nYCbCr Matrix: tv.2020\r\n",L"[script info]\r\nYCbCr Matrix: pc.601\r\n",
            L"[script info]\r\nYCbCr Matrix: pc.709\r\n",L"[script info]\r\nYCbCr Matrix: pc.2020\r\n",
            L"[script info]\r\nYCbCr Matrix: pc.709\r\nYCbCr Matrix: unknown\r\n",
            L"[script info]\r\nfontname: empty\r\n[Events]\r\nPlayResX: 1280\r\n",
            L"[script info]\r\nScriptType: unknown\r\nDialogue: Marked=0,0:00:00.00,0:00:01.00,Default,,0,0,0,,legacy\r\n",
            L"[Events]\r\nDialogue: bad\r\nPlayResX: 1280\r\n",
            L"[v4+ styles]\r\nStyle: broken\r\nPlayResX: 1280\r\n",
        };
        for(auto text:texts) ass_case(index++,CStringW(text),CSize(0,0));
        // Mixed sections change style version independently of dialogue version.
        for(int version=3;version<=6;++version) for(int styleVersion=4;styleVersion<=6;++styleVersion) {
            CStringW text; text.Format(L"[Script Info]\r\nScriptType: v4.00%s\r\n[v4%s styles]\r\nScriptType: unknown\r\n",
                version==3?L"unknown":version==4?L"":version==5?L"+":L"++",styleVersion==4?L"":styleVersion==5?L"+":L"++");
            text+=styleVersion==4?L"Style: Default,Arial,20,1,2,3,4,-1,0,1,2,3,2,1,2,3,0,1\r\n":
                styleVersion==5?L"Style: Default,Arial,20,1,2,3,4,-1,0,0,0,100,100,0,0,1,2,3,2,1,2,3,1\r\n":
                L"Style: Default,Arial,20,1,2,3,4,-1,0,0,0,100,100,0,0,1,2,3,2,1,2,3,4,1,0\r\n";
            text+=version<=4?L"Dialogue: Marked=9,0:00:01.20,0:00:02.30,*Default,Actor,1,2,3,,text\r\n":
                version==5?L"Dialogue: 9,0:00:01.20,0:00:02.30,*Default,Actor,1,2,3,,text\r\n":
                L"Dialogue: 9,0:00:01.20,0:00:02.30,*Default,Actor,1,2,3,4,,text\r\n";
            ass_case(index++,text,CSize(0,0));
        }
        printf("ass-complete cases=%u\n",index);
        return 0;
    } catch(const std::exception& error) {fprintf(stderr,"unresolved: %s\n",error.what());return 1;}
      catch(...) {fprintf(stderr,"unresolved exception\n");return 2;}
}
