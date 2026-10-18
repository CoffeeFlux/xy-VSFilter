// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup parser implementations and initialized document observer.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>
static void text_case(unsigned index,unsigned mode,const CStringW& input,bool preload) {
 TraceDocument document(0);document.Screen(CSize(0,0));
 if(preload)document.Add(L"existing",true,0,10000000,L"Default",L"",L"",CRect(0,0,0,0),0);
 const CStringW encoded=CStringW(L"\xfeff")+input;CTextFile file;
 if(!file.OpenMemory(static_cast<LPCWSTR>(encoded),encoded.GetLength()*sizeof(WCHAR)))throw std::runtime_error("fixture open failed");
 int outcome;try{outcome=(mode==0?OpenSubRipper(&file,document,ANSI_CHARSET):OpenMPL2(&file,document,ANSI_CHARSET))?1:0;}catch(int value){outcome=-100-value;}
 printf("text-case index=%u outcome=%d position=%llu\n",index,outcome,file.GetPosition());document.Dump("text-result");
}
int main(){try{
 if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
 printf("text-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());unsigned index=0;
 auto emit=[&](unsigned mode,const CStringW& text){for(int preload=0;preload<2;++preload)text_case(index++,mode,text,preload!=0);};
 const wchar_t* headers[]={L"",L"1",L"junk",L"0:00:01,234 --> 0:00:04,567",L"0:00:01 --> 0:00:04",L"0x00x01.234 --> 0x00x04.567",L"-1:-2:-3,-4 --> 0:0:5,006",L"0:0:1,1234 --> 0:0:4,5678",L"0:0:1,234-->0:0:4,567",L"0:0:1,234 -> 0:0:4,567",L"0:0:1,234 --> 0:0:4",L"1\r\n0:0:1,000 --> 0:0:4,000"};
 const wchar_t* bodies[]={L"caption",L"",L"1",L"first\r\n\r\n2\r\nnext",L"first\r\n\r\ntext\r\n3",L"<b>bold</b>",L"\t caption ",L"first\r\n\r\n2\r\n0:0:5,000 --> 0:0:6,000\r\nsecond"};
 for(auto head:headers)for(auto body:bodies)for(int newline=0;newline<2;++newline){CStringW text(head);text+=L"\r\n";text+=body;if(newline)text+=L"\r\n";emit(0,text);}
 const wchar_t* values[]={L"",L"junk",L"0",L"-1",L"1",L"2147483647",L"-2147483648",L"999999999999999999",L"+001",L"\t2"};
 for(int field=0;field<8;++field)for(auto value:values){
  const wchar_t* fields[]={L"0",L"0",L"1",L"234",L"0",L"0",L"4",L"567"};fields[field]=value;
  CStringW text;text.Format(L"%s:%s:%s,%s --> %s:%s:%s,%s\r\ncaption\r\n",fields[0],fields[1],fields[2],fields[3],fields[4],fields[5],fields[6],fields[7]);emit(0,text);
 }
 CStringW rawbase(L"1\r\n0:00:01,234 --> 0:00:04,567\r\n caption\r\n\r\n2\r\n0:00:05,000 --> 0:00:06,000\r\nsecond\r\n");
 for(auto unit:{0,9,13,32,0xa0,0x2003,0xd800,0xffff})for(int pos=0;pos<12;++pos){CStringW raw=rawbase;raw.SetAt(pos*4,static_cast<WCHAR>(unit));emit(0,raw);}
 const wchar_t* tails[]={L"]caption",L"]",L"]|/italic",L"caption",L"",L" ]caption",L"]caption\r\n[2][4]second",L"]caption\r\nbad"};
 for(auto start:values)for(auto end:values)for(auto tail:tails)for(int newline=0;newline<2;++newline){CStringW text;text.Format(L"[%s][%s%s%s",start,end,tail,newline?L"\r\n":L"");emit(1,text);}
 CStringW mplraw(L"[10][40] raw |/italic\r\n[41][50]tail\r\n");
 for(auto unit:{0,9,13,32,0xa0,0x2003,0xd800,0xffff})for(int pos=0;pos<12;++pos){CStringW raw=mplraw;raw.SetAt(pos*2,static_cast<WCHAR>(unit));emit(1,raw);}
 printf("text-complete cases=%u\n",index);return 0;
}catch(const std::exception& error){fprintf(stderr,"unresolved: %s\n",error.what());return 1;}catch(...){fprintf(stderr,"unresolved exception\n");return 2;}}
