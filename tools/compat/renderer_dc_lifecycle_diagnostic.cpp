// SPDX-License-Identifier: GPL-2.0-or-later
// Draft baseline observer for the next shared renderer DC ownership port.
#include <stdafx.h>
#include <RTS.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <float.h>
#include <memory>
#include <stdexcept>
static void check(bool b){if(!b)throw std::runtime_error("renderer DC observer failed");}
struct Status {unsigned x87,sse;int error;DWORD last;};
static Status status(){Status s={0,0,errno,GetLastError()};
#ifdef _M_IX86
    _statusfp2(&s.x87,&s.sse);
#else
    s.sse=_statusfp();
#endif
    return s;}
static void reset(){errno=1234;SetLastError(0x12345678);_clearfp();}
static void emit(const char* name,Status s){printf(" %s=%x,%x,%d,%lu",name,s.x87,s.sse,s.error,s.last);}
struct Event {const char* name;unsigned handle;uint32_t value,result;Status before,after;};
static Event events[64];static unsigned used,mode,generation;static HDC current;static bool recording;
static Event* begin(const char* name,HDC dc,uint32_t value=0){if(!recording)return nullptr;check(used<64);check(!dc||dc==current);Event* e=&events[used++];*e=Event{};e->name=name;e->handle=dc?generation:0;e->value=value;e->before=status();return e;}
static void end(Event* e,uint32_t result){if(e){e->result=result;e->after=status();}}
static decltype(&CreateCompatibleDC) realCreate;
static decltype(&SetBkMode) realBk;
static decltype(&SetTextColor) realColor;
static decltype(&SetMapMode) realMap;
static decltype(&DeleteDC) realDelete;
static HDC WINAPI observeCreate(HDC dc){auto e=begin("create",dc);HDC r;if(recording&&mode==1){SetLastError(1111);errno=2334;r=nullptr;}else r=realCreate(dc);if(recording){current=r;if(r)++generation;}end(e,r?generation:0);return r;}
static int WINAPI observeBk(HDC dc,int value){auto e=begin("background",dc,uint32_t(value));int r;if(recording&&mode==2){SetLastError(1112);errno=2335;r=0;}else r=realBk(dc,value);end(e,uint32_t(r));return r;}
static COLORREF WINAPI observeColor(HDC dc,COLORREF value){auto e=begin("color",dc,value);COLORREF r;if(recording&&mode==3){SetLastError(1113);errno=2336;r=CLR_INVALID;}else r=realColor(dc,value);end(e,r);return r;}
static int WINAPI observeMap(HDC dc,int value){auto e=begin("map",dc,uint32_t(value));int r;if(recording&&mode==4){SetLastError(1114);errno=2337;r=0;}else r=realMap(dc,value);end(e,uint32_t(r));return r;}
static BOOL WINAPI observeDelete(HDC dc){auto e=begin("delete",dc);BOOL r=realDelete(dc);end(e,uint32_t(r));return r;}
class Import {
    ULONG_PTR* slot;ULONG_PTR original;
    void put(ULONG_PTR value){DWORD old;check(VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&old)!=FALSE);*slot=value;DWORD ignored;check(VirtualProtect(slot,sizeof(*slot),old,&ignored)!=FALSE);}
public:
    template<class F>Import(const char* name,F replacement,F& saved):slot(nullptr),original(0){
        auto base=reinterpret_cast<unsigned char*>(GetModuleHandle(NULL));auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);check(dos->e_magic==IMAGE_DOS_SIGNATURE);
        auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);check(nt->Signature==IMAGE_NT_SIGNATURE);
        auto imports=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
        for(auto d=imports;d->Name;++d){if(!d->OriginalFirstThunk)continue;auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+d->OriginalFirstThunk);auto values=reinterpret_cast<IMAGE_THUNK_DATA*>(base+d->FirstThunk);
            for(unsigned i=0;names[i].u1.AddressOfData;++i){if(IMAGE_SNAP_BY_ORDINAL(names[i].u1.Ordinal))continue;
                auto entry=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names[i].u1.AddressOfData);
                if(strcmp(reinterpret_cast<const char*>(entry->Name),name)==0){check(slot==nullptr);slot=&values[i].u1.Function;}
            }
        }
        check(slot!=nullptr);original=*slot;saved=reinterpret_cast<F>(original);put(reinterpret_cast<ULONG_PTR>(replacement));
    }
    ~Import(){put(original);}
    Import(const Import&)=delete;Import& operator=(const Import&)=delete;
};

int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);CCritSec lock;
    Import a("CreateCompatibleDC",observeCreate,realCreate),b("SetBkMode",observeBk,realBk),c("SetTextColor",observeColor,realColor),d("SetMapMode",observeMap,realMap),e("DeleteDC",observeDelete,realDelete);
    printf("renderer-dc pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    for(mode=0;mode<5;++mode){
        used=generation=0;current=nullptr;Status phases[8];unsigned counts[8];recording=true;
        reset();auto first=std::make_unique<CRenderedTextSubtitle>(&lock);phases[0]=status();counts[0]=used;
        reset();auto second=std::make_unique<CRenderedTextSubtitle>(&lock);phases[1]=status();counts[1]=used;
        reset();first.reset();phases[2]=status();counts[2]=used;
        reset();auto third=std::make_unique<CRenderedTextSubtitle>(&lock);phases[3]=status();counts[3]=used;
        reset();second.reset();phases[4]=status();counts[4]=used;
        reset();third.reset();phases[5]=status();counts[5]=used;
        reset();auto fourth=std::make_unique<CRenderedTextSubtitle>(&lock);phases[6]=status();counts[6]=used;
        reset();fourth.reset();phases[7]=status();counts[7]=used;recording=false;
        printf("dc-case mode=%u events=%u\n",mode,used);
        for(unsigned i=0;i<8;++i){printf("dc-phase index=%u events=%u",i,counts[i]);emit("status",phases[i]);printf("\n");}
        for(unsigned i=0;i<used;++i){const auto& v=events[i];printf("dc-call name=%s handle=%u value=%u result=%u",v.name,v.handle,v.value,v.result);emit("before",v.before);emit("after",v.after);printf("\n");}
    }
    printf("renderer-dc-complete cases=5\n");return 0;
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
