// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup parser implementations and initialized document observer.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>
static void text_case(unsigned index,unsigned mode,const CStringW& input,bool preload,int charset=ANSI_CHARSET,bool ansi=false) {
 TraceDocument document(0);document.Screen(CSize(0,0));
 if(preload)document.Add(L"existing",true,0,10000000,L"Default",L"",L"",CRect(0,0,0,0),0);
 const CStringW encoded=CStringW(L"\xfeff")+input;CTextFile file;
 std::vector<BYTE> bytes;
 if(ansi){for(int i=0;i<input.GetLength();++i)bytes.push_back(static_cast<BYTE>(input[i]));}
 const void* data=ansi?static_cast<const void*>(bytes.data()):static_cast<LPCWSTR>(encoded);
 const size_t size=ansi?bytes.size():encoded.GetLength()*sizeof(WCHAR);
 if(!file.OpenMemory(data,size))throw std::runtime_error("fixture open failed");
 int outcome;try{outcome=(mode==0?OpenSubRipper(&file,document,charset):mode==1?OpenMPL2(&file,document,charset):mode==2?OpenOldSubRipper(&file,document,charset):OpenVPlayer(&file,document,charset))?1:0;}catch(int value){outcome=-100-value;}
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

 // Each new format exercises partial scans, missing final punctuation, overflow,
 // raw code units, false-read fragments, and pre-existing document acceptance.
 const wchar_t* oldheads[]={L"",L"junk",L"{0:0:1}{0:0:4}",L"{0:0:1}{0:0:4",L"{0:0:1}{0:0:",L"{0:0:1}0:0:4}",L"{0:0:4}{0:0:1}",L"{0:0:1|}{0:0:4}"};
 const wchar_t* vpheads[]={L"",L"junk",L"0:0:1:",L"0:0:1",L"0:0:",L"0:0:1 :",L"-1:-2:-3:",L"0:0:1|:"};
 const wchar_t* captions[]={L"",L"caption",L" |one||two| ",L"\t caption \t",L"{\\fe128}pipe|text",L" caption\r\nbad",L" :extra}text",L"\r\n\r\n"};
 for(unsigned mode=2;mode<=3;++mode){
  for(int hi=0;hi<8;++hi)for(auto caption:captions)for(int newline=0;newline<2;++newline){CStringW text((mode==2?oldheads:vpheads)[hi]);text+=caption;if(newline)text+=L"\r\n";emit(mode,text);}
  for(int field=0;field<(mode==2?6:3);++field)for(auto value:values){
   const wchar_t* fields[]={L"0",L"0",L"1",L"0",L"0",L"4"};fields[field]=value;
   CStringW text;
   if(mode==2)text.Format(L"{%s:%s:%s}{%s:%s:%s}caption|tail\r\n",fields[0],fields[1],fields[2],fields[3],fields[4],fields[5]);
   else text.Format(L"%s:%s:%s:caption|tail\r\n",fields[0],fields[1],fields[2]);
   emit(mode,text);
  }
  CStringW rawbase=mode==2?L"{0:0:1}{0:0:4} raw||tail\r\n":L"0:0:1: raw||tail\r\n";
  for(auto unit:{0,9,13,32,0x81,0x7c,0xa0,0x2003,0xd800,0xffff})for(int pos=0;pos<rawbase.GetLength()-2;++pos){CStringW raw=rawbase;raw.SetAt(pos,static_cast<WCHAR>(unit));emit(mode,raw);}
  const wchar_t* prefixes[]={L"",L"{\\fe128}",L"{\\fe0}",L"{\\fe}",L"{\\fe128}{\\fe0}",L"{\\fe0}{\\fe128}"};
  for(auto charset:{ANSI_CHARSET,SHIFTJIS_CHARSET,GB2312_CHARSET})for(auto prefix:prefixes)for(int preload=0;preload<2;++preload){
   CStringW text=mode==2?L"{0:0:1}{0:0:4}":L"0:0:1:";text+=prefix;text+=L"A\x81|B|C\r\n";
   text_case(index++,mode,text,preload!=0,charset,true);
  }
 }
 printf("text-complete cases=%u\n",index);return 0;
}catch(const std::exception& error){fprintf(stderr,"unresolved: %s\n",error.what());return 1;}catch(...){fprintf(stderr,"unresolved exception\n");return 2;}}
