// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup parser implementations and initialized document observer.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>
static void microdvd_case(unsigned index,const CStringW& input,bool preload,int charset=ANSI_CHARSET,bool ansi=false) {
 TraceDocument document(0);document.Screen(CSize(0,0));
 if(preload)document.Add(L"existing",true,0,10000000,L"Default",L"",L"",CRect(0,0,0,0),0);
 const CStringW encoded=CStringW(L"\xfeff")+input;CTextFile file;
 std::vector<BYTE> bytes;
 if(ansi){for(int i=0;i<input.GetLength();++i)bytes.push_back(static_cast<BYTE>(input[i]));}
 const void* data=ansi?static_cast<const void*>(bytes.data()):static_cast<LPCWSTR>(encoded);
 const size_t size=ansi?bytes.size():encoded.GetLength()*sizeof(WCHAR);
 if(!file.OpenMemory(data,size))throw std::runtime_error("fixture open failed");
 int outcome;try{outcome=OpenMicroDVD(&file,document,charset)?1:0;}catch(int value){outcome=-100-value;}
 printf("microdvd-case index=%u outcome=%d position=%llu\n",index,outcome,file.GetPosition());document.Dump("microdvd-result");
}

int main(){try{
 if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
 printf("microdvd-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());unsigned index=0;
 auto emit=[&](const CStringW& text){for(int preload=0;preload<2;++preload)microdvd_case(index++,text,preload!=0);};
 const wchar_t* heads[]={L"",L"junk",L"{1}{4}",L"{1}{4",L"{1}{}",L"{1}",L"{1",L"{1}junk",L"{1}{bad}",L"{4}{1}",L"{-1}{4}",L"{0}{0}",L"{}{}",L"{STYLE}",L"{a}",L"{{}",L"{1}{2}{3}"};
 const wchar_t* payloads[]={L"",L"caption",L" |one||two| ",L"\t caption \t",L"{y:i}italic",L"first\r\n{2}{5}next",L"first\r\n{2}{}next\r\n{3}{5}last",L"first\r\nbad"};
 for(auto head:heads)for(auto payload:payloads)for(int newline=0;newline<2;++newline){CStringW text(head);text+=payload;if(newline)text+=L"\r\n";emit(text);}
 const wchar_t* values[]={L"",L"junk",L"0",L"-1",L"1",L"2147483647",L"-2147483648",L"999999999999999999",L"+001",L"\t2"};
 const wchar_t* tails[]={L"}caption",L"}",L"}|{y:i}italic",L"caption",L"",L" }caption",L"}caption\r\n{2}{4}second",L"}caption\r\nbad"};
 for(auto start:values)for(auto end:values)for(auto tail:tails)for(int newline=0;newline<2;++newline){CStringW text;text.Format(L"{%s}{%s%s%s",start,end,tail,newline?L"\r\n":L"");emit(text);}
 const wchar_t* names[]={L"a",L"STYLE",L"default",L"DeFaUlT",L" style ",L"123x",L"\u00df",L"\u0131\u0130"};
 const wchar_t* tags[]={L"",L"{f:Arial}",L"{s:24}",L"{h:128}",L"{c:$abcdef}",L"{y:bius}",L"{p:0}",L"{p:1}{s:18.5}",L"{f:Tahoma}{c:$102030}{h:0}"};
 const wchar_t* prefixes[]={L"",L"{0}{}first\r\n",L"{0}{8}first\r\n"};
 for(auto name:names)for(auto tag:tags)for(auto prefix:prefixes){CStringW text;text.Format(L"%s{%s}%s\r\n{2}{8}second\r\n{3}{9}third\r\n",prefix,name,tag);emit(text);}
 const wchar_t* sequences[]={
  L"{0}{}first\r\n{2}{8}second\r\n",L"{0}{}first\r\n{2}{}\r\n{4}{8}third\r\n",
  L"{0}{}first\r\n{-1}{8}second\r\n",L"{0}{}\r\n{2}{8}second\r\n{4}{9}third\r\n",
  L"{style}{y:b}\r\n{0}{8}first\r\n{2}{9}second\r\n",L"{style}{y:b}\r\n{style}{y:b}\r\n{0}{}first\r\n{2}{8}second\r\n",
  L"{Default}\r\n{Default}\r\n{0}{}first\r\n{2}{8}second\r\n",L"{0}{}first\r\n{style}{h:128}\r\n{2}{8}second\r\n{3}{9}third\r\n",
  L"{2147483647}{}first\r\n{2}{8}second\r\n",L"{0}{}first\r\n{2}{8}ignored"};
 for(auto text:sequences)emit(text);
 const CStringW rawbase(L"{10}{} raw||tail\r\n{20}{40}next\r\n");
 for(auto unit:{0,9,13,32,0xa0,0x2003,0xd800,0xffff})for(int pos=0;pos<20;++pos){CStringW raw=rawbase;raw.SetAt(pos,static_cast<WCHAR>(unit));emit(raw);}
 const wchar_t* ansi_prefixes[]={L"",L"{style}{h:128}\r\n",L"{style}{h:0}\r\n"};
 const wchar_t* ansi_tails[]={L"A\x81|B|C",L"{h:128}A\x81|B|C",L"{h:0}A\x81|B|C",L"{H:128}A\x81|B|C"};
 for(auto charset:{ANSI_CHARSET,SHIFTJIS_CHARSET,GB2312_CHARSET})for(auto prefix:ansi_prefixes)for(auto tail:ansi_tails)for(int preload=0;preload<2;++preload){CStringW text;text.Format(L"%s{1}{}%s\r\n{3}{8}last\r\n",prefix,tail);microdvd_case(index++,text,preload!=0,charset,true);}
 printf("microdvd-complete cases=%u\n",index);return 0;
}catch(const std::exception& error){fprintf(stderr,"unresolved: %s\n",error.what());return 1;}catch(...){fprintf(stderr,"unresolved exception\n");return 2;}}
