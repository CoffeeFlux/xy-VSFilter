// SPDX-License-Identifier: GPL-2.0-or-later
// Actual renderer font constructor/cache and Windows objects; no copied methods.
#include <stdafx.h>
#include <RTS.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <float.h>
#include <memory>
#include <vector>
#include <stdexcept>

static void check(bool b){if(!b)throw std::runtime_error("font observer failed");}
static void bytes(const void* p,size_t n){const auto* b=static_cast<const unsigned char*>(p);for(size_t i=0;i<n;++i)printf("%02x",b[i]);}
struct Status {unsigned fp;int error;DWORD last;};
static void reset(){errno=1234;SetLastError(0x12345678);_clearfp();}
static Status status(){Status s={_statusfp(),errno,GetLastError()};return s;}
static void emit(const char* phase,Status s){printf(" %s=%x,%d,%lu",phase,s.fp,s.error,s.last);}
static unsigned sequence=0;
static void run(const STSStyleBase& style,HDC dc){
    reset();auto a=std::make_shared<FwCMyFont>(style);Status first=status();
    reset();auto b=std::make_shared<FwCMyFont>(style);Status second=status();
    reset();auto c=std::make_shared<FwCMyFont>(*a);Status copied=status();
    reset();HFONT ha=a->get(),hb=b->get(),hc=c->get();int ascent=a->get().m_ascent,descent=a->get().m_descent;Status read=status();
    LOGFONTW lf={};check(GetObjectW(ha,sizeof(lf),&lf)==sizeof(lf));
    HGDIOBJ previous=SelectObject(dc,ha);check(previous!=NULL&&previous!=HGDI_ERROR);
    TEXTMETRICW tm={};check(GetTextMetricsW(dc,&tm)!=FALSE);
    const wchar_t sample[]={L'A',L'V',L' ',L'g',0xd800,L'Z'};SIZE extent={};
    check(GetTextExtentPoint32W(dc,sample,6,&extent)!=FALSE);check(SelectObject(dc,previous)!=NULL);
    printf("font-case index=%u count=%llu aliases=%u%u ascent=%d descent=%d extent=%ld,%ld",sequence++,
        static_cast<unsigned long long>(FwCMyFont::Count()),ha==hb,ha==hc,ascent,descent,extent.cx,extent.cy);
    emit("first",first);emit("second",second);emit("copy",copied);emit("read",read);
    printf(" logfont=");bytes(&lf,sizeof(lf));printf(" metric=");bytes(&tm,sizeof(tm));printf("\n");
    reset();a.reset();Status dropa=status();reset();b.reset();Status dropb=status();reset();c.reset();Status dropc=status();
    printf("font-release index=%u count=%llu",sequence-1,static_cast<unsigned long long>(FwCMyFont::Count()));
    emit("a",dropa);emit("b",dropb);emit("c",dropc);
    printf(" deleted=%u%u\n",GetObjectType(ha)==0,GetObjectType(hb)==0);
}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);
    CCritSec lock;CRenderedTextSubtitle renderer(&lock);
    HDC dc=CreateCompatibleDC(NULL);check(dc!=NULL);
    printf("font-provenance pointer_bits=%u ansi=%u oem=%u logfont=%u metric=%u\n",unsigned(sizeof(void*)*8),GetACP(),GetOEMCP(),unsigned(sizeof(LOGFONTW)),unsigned(sizeof(TEXTMETRICW)));
    std::vector<CStringW> names={L"Arial",L"Times New Roman",L"Courier New",L"Segoe UI",L"xy-no-such-font-compat",L"",L"@Arial",L"arial"};
    for(int n:{30,31,32,70})names.push_back(CStringW(L'x',n));
    const wchar_t a[]={L'A',L'r',L'i',L'a',L'l',0,L'X'};names.push_back(CStringW(a,7));
    const wchar_t b[]={0,L'X'};names.push_back(CStringW(b,2));
    const wchar_t c[]={0xd800,0xdc00,0xffff};names.push_back(CStringW(c,3));
    names.push_back(CStringW(L' ',3));
    for(const auto& name:names)for(double size:{-64.,-1.,-0.,0.,.5,1.,8.25,20.,127.75}){STSStyle s;s.fontName=name;s.fontSize=size;run(s,dc);}
    for(unsigned flags=0;flags<32;++flags){STSStyle s;s.fItalic=!!(flags&1);s.fUnderline=!!(flags&2);s.fStrikeOut=!!(flags&4);s.fontWeight=400+100*(flags>>3);run(s,dc);}
    for(int weight:{-2147483647-1,-1,0,1,399,400,700,1000,2147483647}){STSStyle s;s.fontWeight=weight;run(s,dc);}
    for(int charset:{-2147483647-1,-1,0,1,2,128,255,256,257,2147483647}){STSStyle s;s.charSet=charset;run(s,dc);}
    check(DeleteDC(dc)!=FALSE);printf("font-complete cases=%u count=%llu\n",sequence,static_cast<unsigned long long>(FwCMyFont::Count()));return 0;
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
