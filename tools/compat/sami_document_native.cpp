// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup parser implementations and initialized document observer.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>
static void sami_case(unsigned index,const CStringW& input,bool preload,int charset=ANSI_CHARSET,bool ansi=false,bool skip_line=false) {
 TraceDocument document(0);document.Screen(CSize(0,0));
 if(preload)document.Add(L"existing",true,0,10000000,L"Default",L"",L"",CRect(0,0,0,0),0);
 const CStringW encoded=CStringW(L"\xfeff")+input;CTextFile file;
 std::vector<BYTE> bytes;
 if(ansi){for(int i=0;i<input.GetLength();++i)bytes.push_back(static_cast<BYTE>(input[i]));}
 const void* data=ansi?static_cast<const void*>(bytes.data()):static_cast<LPCWSTR>(encoded);
 const size_t size=ansi?bytes.size():encoded.GetLength()*sizeof(WCHAR);
 if(!file.OpenMemory(data,size))throw std::runtime_error("fixture open failed");
 if(skip_line){CStringW discarded;file.ReadString(discarded);}
 int outcome;try{outcome=OpenSami(&file,document,charset)?1:0;}catch(int value){outcome=-100-value;}
 printf("sami-case index=%u outcome=%d position=%llu\n",index,outcome,file.GetPosition());document.Dump("sami-result");
}


int main(){try{
 if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
 printf("sami-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());unsigned index=0;
 auto emit=[&](const CStringW& text){for(int preload=0;preload<2;++preload)sami_case(index++,text,preload!=0);};
 const wchar_t* markers[]={L"",L"junk",L"<sami>",L"<SAMI>",L"before<SAMI>after",L"<sami >",L"<SAMI",L"<!--<SAMI>-->",L"<title><sami></title>",L"<SAM\u0130>"};
 const wchar_t* bodies[]={L"",L"caption",L"<SYNC START=1000>caption",L"<SYNC START=1000>first\r\n<SYNC START=2000>second",L"<SYNC START=1000>first\r\n<SYNC START=2000>",L"<SYNC START=1000><b>bold</b>",L"<SYNC START=0>one\r\ntwo",L"<SYNC START=0>&nbsp;",L"<SYNC START=0><font color=red>red</font>",L"<SYNC START=0>one<br>two\r\n</sami>"};
 for(auto marker:markers)for(auto body:bodies)for(int newline=0;newline<2;++newline){CStringW text(marker);text+=L"\r\n";text+=body;if(newline)text+=L"\r\n";emit(text);}
 const wchar_t* prefixes[]={L"",L"x",L"12345678901",L"123456789012",L"<P>",L"<BODY>",L"   ",L"<SYNC START=4>old"};
 const wchar_t* times[]={L"",L"0",L"-1",L"1000",L"0002",L"1a2b3",L"2147483647",L"2147483648",L"4294967296",L"999999999999999999999",L"\u0661\u0662",L"\uff11\uff12"};
 const wchar_t* tails[]={L">A",L">",L"M>A",L"ms>A",L"x>A",L">one<SYNC START=9>two",L">A\r\n<SYNC START=3000>B",L">A\r\n<!--ignored-->\r\n<SYNC START=4000>B"};
 for(auto prefix:prefixes)for(auto time:times)for(auto tail:tails){CStringW text;text.Format(L"<sami>\r\n%s<SYNC START=%s%s\r\n",prefix,time,tail);emit(text);}
 const wchar_t* comments[]={L"<!--",L"<!--closed-->",L"<TITLE>",L"<TITLE>closed</TITLE>",L"-->",L"</TITLE>",L"<!--<TITLE>",L"<TITLE><!--",L"<TITLE></TITLE><!--",L"<!-- --><TITLE>"};
 const wchar_t* endings[]={L"",L"-->",L"</TITLE>",L"<SYNC START=2000>hidden-->",L"<SYNC START=2000>hidden</TITLE>",L"-->tail</TITLE>"};
 for(auto comment:comments)for(auto ending:endings){CStringW text;text.Format(L"<sami>\r\n<SYNC START=1000>first\r\n%s\r\n%s\r\n<SYNC START=3000>last\r\n",comment,ending);emit(text);}
 const CStringW rawbase(L"<sami>\r\n<SYNC START=123>first<br>second\r\n<SYNC START=4567>tail\r\n</sami>\r\n");
 for(auto unit:{0,9,13,32,0xa0,0x2003,0x0661,0xff11,0xd800,0xffff})for(int pos=0;pos<32;++pos){CStringW raw=rawbase;raw.SetAt(pos*2,static_cast<WCHAR>(unit));emit(raw);}
 const wchar_t* sequences[]={L"<sami>",L"<sami>\r\n",L"<sami>\r\nfragment",L"<sami>\r\n<SYNC START=1>fragment",L"<sami>\r\n<!--fragment",L"<sami>\r\n<SYNC START=1>first\r\n<SYNC START=2>fragment",L"<sami>\r\n<SYNC START=1>first\r\n \t",L"<sami>\r\n<SYNC START=1>first\r\n\r\n",L"<sami>\r\n<SYNC START=2>first\r\n<SYNC START=1>last\r\n",L"<sami>\r\n<SYNC START=1>first\r\n<SYNC START=1>last\r\n",L"<sami>\r\n<SYNC START=1>first\r\n<!--open\r\n",L"<sami>\r\nplain\r\ncaption\r\n"};
 for(auto text:sequences)emit(text);
 const wchar_t* ansi[]={L"A\x81|B",L"caf\xe9",L"<b>A\x81|B</b>",L"{\\fe128}A\x81|B",L"&nbsp;\x81|",L"\x82\xa0\x82\xa2"};
 for(auto charset:{ANSI_CHARSET,SHIFTJIS_CHARSET,GB2312_CHARSET})for(auto body:ansi)for(int preload=0;preload<2;++preload){CStringW text;text.Format(L"<sami>\r\n<SYNC START=1000>%s\r\n<SYNC START=2000>tail\r\n",body);sami_case(index++,text,preload!=0,charset,true);}
 for(auto marker:markers)for(int skip=0;skip<2;++skip)for(int preload=0;preload<2;++preload)for(int newline=0;newline<2;++newline){CStringW text;text.Format(L"discarded\r\n%s\r\n<SYNC START=123>caption%s",marker,newline?L"\r\n":L"");sami_case(index++,text,preload!=0,ANSI_CHARSET,false,skip!=0);}
 printf("sami-complete cases=%u\n",index);return 0;
}catch(const std::exception& error){fprintf(stderr,"unresolved: %s\n",error.what());return 1;}catch(...){fprintf(stderr,"unresolved exception\n");return 2;}}
