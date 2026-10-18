// SPDX-License-Identifier: GPL-2.0-or-later
// Investigation only: do not interpret uninitialized reference reads as parity.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>
__declspec(noinline) static void prime_stack(unsigned char value) {
 volatile unsigned char scratch[8192];
 for(size_t i=0;i<sizeof scratch;++i)scratch[i]=value;
}
__declspec(noinline) static void observe(unsigned ordinal,const wchar_t* prefix) {
 TraceDocument document(0);document.Screen(CSize(0,0));CTextFile file;
 CStringW input;input.Format(L"\xfeff%s00:00:01.00,00:00:02.00\r\ncaption\r\n",prefix);
 if(!file.OpenMemory(static_cast<LPCWSTR>(input),input.GetLength()*sizeof(WCHAR)))throw std::runtime_error("open fixture");
 const bool accepted=OpenSubViewer(&file,document,ANSI_CHARSET);
 printf("case=%u accepted=%u entries=%zu",ordinal,accepted?1:0,document.Entries());
 if(document.Entries())units(document.Entry(0).str);
 printf("\n");
}
int main(){try{
 if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
 printf("subviewer-reference pointer_bits=%zu\n",sizeof(void*)*8);
 for(unsigned repeat=0;repeat<8;++repeat){
  prime_stack(static_cast<unsigned char>(repeat*37));observe(repeat*4,L"[STYLE]no\r\n");
  prime_stack(static_cast<unsigned char>(repeat*37));observe(repeat*4+1,L"");
  prime_stack(static_cast<unsigned char>(repeat*37));observe(repeat*4+2,L"[STYLE]bd\r\n");
  prime_stack(static_cast<unsigned char>(repeat*37));observe(repeat*4+3,L"");
 }
 return 0;
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 1;}catch(...){return 2;}}
