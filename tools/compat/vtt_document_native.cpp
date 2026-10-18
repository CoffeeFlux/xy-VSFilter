// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup OpenVTT and initialized document observer, including byte consumption.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>
static void vtt_case(unsigned index,const CStringW& input,bool preload,int charset=ANSI_CHARSET,bool ansi=false) {
 TraceDocument document(0);document.Screen(CSize(0,0));
 if(preload)document.Add(L"existing",true,0,10000000,L"Default",L"",L"",CRect(0,0,0,0),0);
 const CStringW encoded=CStringW(L"\xfeff")+input;CTextFile file;std::vector<BYTE> bytes;
 if(ansi){for(int i=0;i<input.GetLength();++i)bytes.push_back(static_cast<BYTE>(input[i]));}
 const void* data=ansi?static_cast<const void*>(bytes.data()):static_cast<LPCWSTR>(encoded);
 const size_t size=ansi?bytes.size():encoded.GetLength()*sizeof(WCHAR);
 if(!file.OpenMemory(data,size))throw std::runtime_error("fixture open failed");
 int outcome;try{outcome=OpenVTT(&file,document,charset)?1:0;}catch(int value){outcome=-100-value;}
 printf("vtt-case index=%u outcome=%d position=%llu\n",index,outcome,file.GetPosition());document.Dump("vtt-result");
}
int main(){try{
 if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
 printf("vtt-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());unsigned index=0;
 auto emit=[&](const CStringW& text){for(int preload=0;preload<2;++preload)vtt_case(index++,text,preload!=0);};
 const wchar_t* magic[]={L"",L"WEBVTT",L"WEBVTTjunk",L" WEBVTT",L"webvtt",L"WEBVT",L"WEBVTT \t",L"WEBVTT\r\nSTYLE"};
 const wchar_t* bodies[]={L"",L"caption",L"00:01.000 --> 00:02.000\r\ncaption",L"00:01.000 --> 00:02.000\r\n<b>bold</b>",L"00:01.000 --> 00:02.000 align:end\r\none\r\ntwo",L"bad\r\n00:01.000 --> 00:02.000\r\ncaption\r\n\r\n"};
 for(auto m:magic)for(auto b:bodies)for(int nl=0;nl<2;++nl){CStringW text;text.Format(L"%s\r\n%s%s",m,b,nl?L"\r\n":L"");emit(text);}
 const wchar_t* times[]={L"",L"0",L"00:01",L"00:01.000",L"00:00:01.000",L"1a2b3",L"1a2b3c",L"1a2b3c4",L"1:2.3tail",L"-1:-2.-3",L"2147483647:2.3",L"2147483648:2.3",L"4294967296:2.3",L"00:99.9999",L"1:2:3:4:5",L"+1:+2.+3",L"\uff11:2.3",L"1::2.3",L"1:2.",L"1:2.3s"};
 for(auto a:times)for(auto b:times){CStringW text;text.Format(L"WEBVTT\r\n%s --> %s\r\ncaption\r\n\r\n00:03.000 --> 00:04.000\r\nrecovered\r\n",a,b);emit(text);}
 const wchar_t* styles[]={L"",L"::cue {color:red;}",L"::cue {background:blue;}",L"::cue {color:red;} ::cue {color:blue;}",L"::cue(.red) {color:lime;}",L"::cue(.red) {background:white;}",L"::cue(.x) {color:rgb(1,2,3);}",L"::cue(.x) {color:rgb(256,257,258);}",L"::cue(.x) {color:rgb(2147483647,0,1);}",L"::cue {color:#aBcD01;}",L"::cue {color:;}",L"::cue {background-color:red;}",L"::cue {color:red}",L"::cue {Color:red;}",L"::cue(.x) {color:red;} ::cue(.x) {background:blue;}",L"/* comment */::cue {color:red;}",L"::cue {/* comment */color:red;}",L"/* unclosed ::cue {color:red;}",L"/*/*/::cue {color:red;}",L"::cue(.x) {color:red;}\r\n \t\r\n::cue(.x) {color:blue;}",L"::cue {color:red;}\r\n\r\nStyle:\r\n::cue {background:blue;}",L"::cue {color:red;}\r\nSTYLE",L"::cue(v[voice=Bob]) {color:red;}",L"::cue(.x) {color:rgb(0,0,0); color:red;}"};
 const wchar_t* captions[]={L"plain",L"<c.red>red</c>",L"<c.x>x<c.bg_white>bg</c>end</c>",L"<v Bob>voice</v>",L"<b>bold</b>&amp;&lt;&nbsp;",L"<c.x>first\r\nsecond</c>"};
 for(auto s:styles)for(auto c:captions){CStringW text;text.Format(L"WEBVTT\r\nSTYLE\r\n%s\r\n00:01.000 --> 00:02.000 align:start\r\n%s\r\n\r\n",s,c);emit(text);}
 const wchar_t* sequence[]={L"WEBVTT",L"WEBVTT\r\nSTYLE\r\n::cue {color:red;}\r\n00:01.000 --> 00:02.000",L"WEBVTT\r\nSTYLE\r\n::cue {color:red;}",L"WEBVTT\r\nStyle:\r\n \t\r\n00:01.000 --> 00:02.000\r\ncaption",L"WEBVTT\r\n00:01.000 --> 00:02.000\r\n\r\nSTYLE\r\n::cue {color:red;}\r\n\r\n00:03.000 --> 00:04.000\r\ncaption\r\n",L"WEBVTT\r\n00:01.000 --> 00:02.000\r\nA\r\n\r\n00:01.000 --> 00:02.000\r\nA\r\n",L"WEBVTT\r\n00:01.000 --> 00:02.000\r\nA\r\n\r\n00:01.000 --> 00:02.000\r\nB\r\n",L"WEBVTT\r\n00:01.000 --> 00:02.000\r\nA\r\n\r\n00:01.000 --> 00:02.000 align:end\r\nA\r\n"};
 for(auto s:sequence)emit(s);
 const CStringW rawbase(L"WEBVTT\r\nSTYLE\r\n::cue {color:red;}\r\n\r\n00:01.000 --> 00:02.000 align:end\r\n<c.red>raw</c>\r\n\r\n00:03.000 --> 00:04.000\r\ntail\r\n");
 for(auto unit:{0,9,13,32,0xa0,0x2003,0x0661,0xff11,0xd800,0xffff})for(int pos=0;pos<48;++pos){CStringW raw=rawbase;raw.SetAt(pos*2,static_cast<WCHAR>(unit));emit(raw);}
 const wchar_t* ansi[]={L"A\x81|B",L"caf\xe9",L"<b>A\x81|B</b>",L"{\\fe128}A\x81|B",L"&nbsp;\x81|",L"\x82\xa0\x82\xa2"};
 for(auto charset:{ANSI_CHARSET,SHIFTJIS_CHARSET,GB2312_CHARSET})for(auto body:ansi)for(int preload=0;preload<2;++preload){CStringW text;text.Format(L"WEBVTT\r\n00:01.000 --> 00:02.000\r\n%s\r\n",body);vtt_case(index++,text,preload!=0,charset,true);}
 printf("vtt-complete cases=%u\n",index);return 0;
}catch(const std::exception& error){fprintf(stderr,"unresolved: %s\n",error.what());return 1;}catch(...){fprintf(stderr,"unresolved exception\n");return 2;}}
