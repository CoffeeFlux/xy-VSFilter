// SPDX-License-Identifier: GPL-2.0-or-later
// Observe actual linked font methods by replacing only this probe executable's
// Windows import slots. Real GDI handles are used; scripted creation failures
// exercise the cleanup's fallback without exhausting system resources.
#define main unused_font_observer_main
#include "font_native.cpp"
#undef main

struct Event {const char* name;uint64_t a,b,c,result;unsigned handles;Status before,after;unsigned length;unsigned char data[92];};
static Event events[64];static unsigned used=0,attempt=0,mode=0;static bool recording=false;
static Event* begin(const char* name,uint64_t a=0,uint64_t b=0,uint64_t c=0,unsigned handles=0){
    if(!recording)return nullptr;if(used==64)abort();Event* e=&events[used++];*e=Event{};e->name=name;e->a=a;e->b=b;e->c=c;e->handles=handles;e->before=status();return e;
}
static void end(Event* e,uint64_t result){if(e){e->result=result;e->after=status();}}
static decltype(&GetDC) realGetDC;
static decltype(&ReleaseDC) realReleaseDC;
static decltype(&GetDeviceCaps) realGetDeviceCaps;
static decltype(&MulDiv) realMulDiv;
static decltype(&CreateFontIndirectW) realCreateFont;
static decltype(&SelectObject) realSelect;
static decltype(&GetTextMetricsW) realMetrics;
static decltype(&DeleteObject) realDelete;
static HDC WINAPI observeGetDC(HWND w){auto e=begin("getdc",reinterpret_cast<uintptr_t>(w),0,0,9);HDC r=realGetDC(w);end(e,reinterpret_cast<uintptr_t>(r));return r;}
static int WINAPI observeReleaseDC(HWND w,HDC dc){auto e=begin("releasedc",reinterpret_cast<uintptr_t>(w),reinterpret_cast<uintptr_t>(dc),0,3);int r=realReleaseDC(w,dc);end(e,r);return r;}
static int WINAPI observeCaps(HDC dc,int n){auto e=begin("caps",reinterpret_cast<uintptr_t>(dc),uint32_t(n),0,1);int r=realGetDeviceCaps(dc,n);end(e,uint32_t(r));return r;}
static int WINAPI observeMulDiv(int a,int b,int c){auto e=begin("muldiv",uint32_t(a),uint32_t(b),uint32_t(c));int r=realMulDiv(a,b,c);end(e,uint32_t(r));return r;}
static HFONT WINAPI observeCreate(const LOGFONTW* lf){
    auto e=begin("create",0,0,0,8);if(e){e->length=sizeof(*lf);memcpy(e->data,lf,sizeof(*lf));}
    HFONT r;if(recording&&attempt++<mode){SetLastError(1111);errno=2334;r=nullptr;}else r=realCreateFont(lf);end(e,reinterpret_cast<uintptr_t>(r));return r;
}
static HGDIOBJ WINAPI observeSelect(HDC dc,HGDIOBJ object){auto e=begin("select",reinterpret_cast<uintptr_t>(dc),reinterpret_cast<uintptr_t>(object),0,11);auto r=realSelect(dc,object);end(e,reinterpret_cast<uintptr_t>(r));return r;}
static BOOL WINAPI observeMetrics(HDC dc,TEXTMETRICW* tm){auto e=begin("metrics",reinterpret_cast<uintptr_t>(dc),0,0,1);BOOL r=realMetrics(dc,tm);end(e,r);if(e&&r){e->length=57;memcpy(e->data,tm,57);}return r;}
static BOOL WINAPI observeDelete(HGDIOBJ object){auto e=begin("delete",reinterpret_cast<uintptr_t>(object),0,0,1);BOOL r=realDelete(object);end(e,r);return r;}

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
static uint64_t identities[64];static unsigned identity_count;
static uint64_t identity(uint64_t handle){if(!handle)return 0;for(unsigned i=0;i<identity_count;++i)if(identities[i]==handle)return i+1;check(identity_count<64);identities[identity_count++]=handle;return identity_count;}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);CCritSec lock;CRenderedTextSubtitle renderer(&lock);
    Import a("GetDC",observeGetDC,realGetDC),b("ReleaseDC",observeReleaseDC,realReleaseDC),c("GetDeviceCaps",observeCaps,realGetDeviceCaps),d("MulDiv",observeMulDiv,realMulDiv),
        e("CreateFontIndirectW",observeCreate,realCreateFont),f("SelectObject",observeSelect,realSelect),g("GetTextMetricsW",observeMetrics,realMetrics),h("DeleteObject",observeDelete,realDelete);
    printf("font-gdi pointer_bits=%u\n",unsigned(sizeof(void*)*8));unsigned index=0;
    for(unsigned name=0;name<4;++name)for(mode=0;mode<3;++mode){
        STSStyle s;s.fontSize=20.25;s.fontName=name==0?CStringW(L"Arial"):name==1?CStringW(L'x',62):name==2?CStringW(L"@Arial"):CStringW(L"Times New Roman");
        used=attempt=identity_count=0;reset();recording=true;
        auto first=std::make_shared<FwCMyFont>(s);auto second=std::make_shared<FwCMyFont>(s);const auto& snap=first->get();
        HFONT font=snap;int ascent=snap.m_ascent,descent=snap.m_descent;first.reset();second.reset();Status final=status();recording=false;
        printf("font-gdi-case index=%u mode=%u events=%u count=%llu font=%u ascent=%d descent=%d",index++,mode,used,static_cast<unsigned long long>(FwCMyFont::Count()),font!=nullptr,ascent,descent);emit("final",final);printf("\n");
        for(unsigned i=0;i<used;++i){const auto& v=events[i];
            // Normalize OS handle identity only; retain all scalar values and
            // raw LOGFONT bytes, including the tail after fallback's new NUL.
            uint64_t aa=v.handles&1?identity(v.a):v.a,bb=v.handles&2?identity(v.b):v.b,cc=v.handles&4?identity(v.c):v.c,rr=v.handles&8?identity(v.result):v.result;
            printf("gdi name=%s a=%llu b=%llu c=%llu result=%llu",v.name,static_cast<unsigned long long>(aa),static_cast<unsigned long long>(bb),static_cast<unsigned long long>(cc),static_cast<unsigned long long>(rr));emit("before",v.before);emit("after",v.after);printf(" data=");bytes(v.data,v.length);printf("\n");
        }
    }
    printf("font-gdi-complete cases=%u\n",index);return 0;
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
