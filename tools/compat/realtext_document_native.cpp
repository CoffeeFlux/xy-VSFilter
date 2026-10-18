// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup parser implementations and initialized document observer.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>
static void realtext_case(unsigned index,const CStringW& input,bool preload,int charset=ANSI_CHARSET,bool ansi=false,bool skip_line=false) {
 TraceDocument document(0);document.Screen(CSize(0,0));
 if(preload)document.Add(L"existing",true,0,10000000,L"Default",L"",L"",CRect(0,0,0,0),0);
 const CStringW encoded=CStringW(L"\xfeff")+input;CTextFile file;
 std::vector<BYTE> bytes;
 if(ansi){for(int i=0;i<input.GetLength();++i)bytes.push_back(static_cast<BYTE>(input[i]));}
 const void* data=ansi?static_cast<const void*>(bytes.data()):static_cast<LPCWSTR>(encoded);
 const size_t size=ansi?bytes.size():encoded.GetLength()*sizeof(WCHAR);
 if(!file.OpenMemory(data,size))throw std::runtime_error("fixture open failed");
 if(skip_line){CStringW discarded;file.ReadString(discarded);}
 int outcome;try{outcome=OpenRealText(&file,document,charset)?1:0;}catch(int value){outcome=-100-value;}
 printf("realtext-case index=%u outcome=%d position=%llu\n",index,outcome,file.GetPosition());document.Dump("realtext-result");
}


int main(){try{
 if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
 printf("realtext-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());unsigned index=0;
 auto emit=[&](const CStringW& text){for(int preload=0;preload<2;++preload)realtext_case(index++,text,preload!=0);};
 const wchar_t* markers[]={L"",L"junk",L"<window>",L"<WINDOW>",L" <window>",L"<window width=100>",L"<window",L"<z>",L"<windoz>"};
 const wchar_t* bodies[]={L"",L"caption",L"<time begin=\"1\"/>caption",L"<time begin=\"1\" end=\"2\"/>caption",L"<time begin=\"1\"/>first<time begin=\"2\"/>second",L"<time begin=\"1\"/><b>bold</b>",L"<time begin=\"1\"/><font color=\"red\">red</font>",L"<time begin=\"1\"/>one<br/>two",L"<time begin=\"1\"/>one<clear/>two",L"before<time begin=\"1\"/>after",L"<time begin=\"0\"/>zero",L"<center><time begin=\"1\"/>center</center>"};
 for(auto m:markers)for(auto b:bodies)for(int nl=0;nl<2;++nl){CStringW text;text.Format(L"%s\r\n%s%s",m,b,nl?L"\r\n":L"");emit(text);}
 const wchar_t* times[]={L"",L"0",L"1",L"-1",L"1.2",L"1:2",L"1:2.3",L"1:2:3.4",L"1::2",L"abc",L"1x",L"2147483647",L"2147483648",L"4294967296",L"99999999999999999",L".5",L"\uff11",L"-1:-2.-3"};
 for(auto a:times)for(auto b:times){CStringW text;text.Format(L"<window>\r\n<time begin=\"%s\" end=\"%s\"/>first<time begin=\"3\"/>last\r\n",a,b);emit(text);}
 const wchar_t* tags[]={L"<b>",L"<i>",L"<font color=\"red\" size=\"20\">",L"<font size=\"13\">",L"<font size=\"26\">",L"<font face=\"Arial\" size=\"big\">",L"<b><b></b>",L"<b/><i/>",L"<i></b>",L"<!--comment-->",L"<unknown>",L"<required>"};
 const wchar_t* sequences[]={L"<time begin=\"1\"/>A<clear/>B",L"<time begin=\"1\"/>A<time begin=\"2\"/>B",L"<time begin=\"2\"/>A<time begin=\"1\"/>B",L"<time begin=\"1\" end=\"2\"/>A<clear/><time begin=\"1\" end=\"2\"/>B",L"<time begin=\"-1\"/>A",L"<time begin=\"1\" end=\"-2\"/>A",L"<time begin=\"2147483.647\"/>A",L"text<clear/><time begin=\"1\"/>tail"};
 for(auto t:tags)for(auto q:sequences){CStringW text;text.Format(L"<window>\r\n%s%s\r\n",t,q);emit(text);}
 const CStringW rawbase(L"<window>\r\n<time begin=\"1\" end=\"2\"/><font color=\"red\">raw</font>\r\n<clear/><time begin=\"3\" end=\"5\"/>tail\r\n");
 for(auto unit:{0,9,13,32,0xa0,0x2003,0x0661,0xff11,0xd800,0xffff})for(int pos=0;pos<44;++pos){CStringW raw=rawbase;raw.SetAt(pos*2,static_cast<WCHAR>(unit));emit(raw);}
 const wchar_t* ansi[]={L"A\x81|B",L"caf\xe9",L"<b>A\x81|B</b>",L"{\\fe128}A\x81|B",L"&nbsp;\x81|",L"\x82\xa0\x82\xa2"};
 for(auto charset:{ANSI_CHARSET,SHIFTJIS_CHARSET,GB2312_CHARSET})for(auto body:ansi)for(int preload=0;preload<2;++preload){CStringW text;text.Format(L"<window>\r\n<time begin=\"1\" end=\"2\"/>%s\r\n",body);realtext_case(index++,text,preload!=0,charset,true);}
 const wchar_t* eof[]={L"",L"<window>",L"<window>\r\n",L"<window>\r\nfragment",L"<window>\r\n<time begin=\"1\"/>first\r\nfragment",L"<window>\r\n<time begin=\"1\"/>first\r\n",L"<window>\r\n \t\r\n<time begin=\"1\"/>a\r\n",L"<window>\r\n<time begin=\"1\"/>   a   \r\n",L"<window>\r\n<time begin=\"1\"/>a\r\n<time begin=\"2\"/>b",L"<window>\r\n<time begin=\"1\"/>a\r\n<time begin=\"2\"/>b\r\n",L"<window>\r\n<time begin=\"1\"/>a\r\n</window>\r\n",L"<window>\r\n<time begin=\"1\"/>a\r\n<!--open\r\n"};
 for(auto t:eof)emit(t);
 // Keep the original 2,212-case prefix. Explicit terminators exercise successful
 // map insertion/export alongside the original trailing-text rejection cases.
 for(auto a:times)for(auto b:times){CStringW text;text.Format(L"<window>\r\n<time begin=\"%s\" end=\"%s\"/>first<time begin=\"3\"/>last</window>\r\n",a,b);emit(text);}
 for(auto t:tags)for(auto q:sequences){CStringW text;text.Format(L"<window>\r\n%s%s</window>\r\n",t,q);emit(text);}
 for(auto unit:{0,9,13,32,0xa0,0x2003,0x0661,0xff11,0xd800,0xffff})for(int pos=0;pos<44;++pos){CStringW raw=rawbase;raw.SetAt(pos*2,static_cast<WCHAR>(unit));raw+=L"</window>\r\n";emit(raw);}
 const wchar_t* attributes[]={L"",L"size=\"13\"",L"size=\"14\"",L"size=\"25\"",L"size=\"26\"",L"size=\"0\"",L"size=\"-1\"",L"size=\"+14\"",L"size=\"2147483648\"",L"size=\"\uff11\uff14\"",L"SiZe=\"20\"",L"size=\"13\" SIZE=\"20\"",L"face=\"Arial\" color=\"red\"",L"color=\"red/blue\"",L"face=' Arial'",L"face=\"x'",L"face= x",L"face x=y",L"face=\"unterminated",L"face=\"\"",L"face=\"  \"",L"\tSIZE\t=\t'20'",L"\u2003size=\"20\"",L"begin=\"1:2:3:4:5:6:7\" end=\"2\"",L"begin=\"1::2.\"",L"begin=\".\" end=\"-1\"",L"begin='1' BEGIN='2' end='5'",L"\u0130=\"value\"",L"\ud800=\"raw\"",L"size='2 0'"};
 for(auto attr:attributes)for(auto name:{L"font",L"b",L"i",L"window",L"TiMe"})for(auto body:{L"First<clear/>Second",L"First</i>"}){CStringW text;text.Format(L"<window>\r\n<time begin=\"1\" end=\"5\"/><%s %s>%s</window>\r\n",name,attr,body);emit(text);}
 printf("realtext-complete cases=%u\n",index);return 0;
}catch(const std::exception& error){fprintf(stderr,"unresolved: %s\n",error.what());return 1;}catch(...){fprintf(stderr,"unresolved exception\n");return 2;}}
