// SPDX-License-Identifier: GPL-2.0-or-later
// Actual CText measurement with scripted initialized extents and raw float bits.
#define XY_TEXT_MEASURE_INCLUDED
#include "renderer_dc_text_native.cpp"
#undef XY_TEXT_MEASURE_INCLUDED
#include <cfenv>
#include <process.h>
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include <RustTextMeasure.h>
#endif
static unsigned history,extent_count;static bool observing;
struct MeasureEvent {const char* name;int count,result,width;Status before,after;};
static MeasureEvent measure_events[16];static unsigned measure_used;
static MeasureEvent* enter(const char* name,int count=0){if(!observing)return nullptr;check(measure_used<16);auto e=&measure_events[measure_used++];*e=MeasureEvent{};e->name=name;e->count=count;e->before=status();return e;}
static void leave(MeasureEvent* e,int result){if(e){e->result=result;e->after=status();}}
static HGDIOBJ WINAPI measure_select(HDC dc,HGDIOBJ object){auto e=enter("select");auto r=text_real_select(dc,object);leave(e,r!=nullptr);return r;}
static BOOL WINAPI measure_extent(HDC dc,LPCWSTR text,int count,LPSIZE size){auto e=enter("extent",count);if(!observing)return text_real_extent(dc,text,count,size);check(dc==text_dc);++extent_count;
    if((history==1&&extent_count==1)||(history==2&&extent_count==2)){SetLastError(5432);errno=4321;leave(e,0);return FALSE;}
    size->cx=history==3?INT_MAX:7;size->cy=19;if(e)e->width=size->cx;leave(e,1);return TRUE;}
static decltype(&GetVersion) measure_real_version;
static DWORD WINAPI measure_version(){auto e=enter("version");auto r=observing&&history==3?0x80000000u:measure_real_version();leave(e,static_cast<int>(r));return r;}
int main(int argc,char** argv){try{
    const bool raw_canonical=argc==2&&!strcmp(argv[1],"raw-canonical");check(argc==1||raw_canonical);
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);CCritSec lock;
    Import a("CreateCompatibleDC",captureCreate,realCreate),b("SelectObject",measure_select,text_real_select),c("GetTextExtentPoint32W",measure_extent,text_real_extent),d("GetVersion",measure_version,measure_real_version);
    CRenderedTextSubtitle renderer(&lock);STSStyle base;base.fontName=L"Arial";base.fontSize=20;FwCMyFont font(base);
    std::vector<std::shared_ptr<FwSTSStyle>> retained;
    const uint64_t raw[]={0,0x8000000000000000ull,0x3ff4000000000000ull,0xbff4000000000000ull,0x4059000000000000ull,0xc059000000000000ull,1,0x0010000000000000ull,0x7fefffffffffffffull,0x7ff0000000000000ull,0xfff0000000000000ull,0x7ff8000000000001ull,0x7ff0000000000001ull,0x41e0000000000000ull,0xc1e0000000000000ull,0x43e0000000000000ull,0x4059000000000001ull,0x4058ffffffffffffull};
    const wchar_t units[]={65,0xd800,66,0,67};const CStringW text(units,5);
    double STSStyle::* fields[]={&STSStyle::fontScaleX,&STSStyle::fontScaleY,&STSStyle::fontSpacing};unsigned index=0;
    printf("text-measure pointer_bits=%u mode=%s\n",unsigned(sizeof(void*)*8),raw_canonical?"raw-canonical":"construction");
    for(unsigned sticky=0;sticky<2;++sticky)for(unsigned field=0;field<3;++field)for(unsigned value=0;value<18;++value){
        STSStyle style=base;if(raw_canonical)style.scrAlignment=1000+index;
        memcpy(&(style.*fields[field]),&raw[value],8);auto owner=std::make_shared<FwSTSStyle>(style);retained.push_back(owner);
        uint64_t original;
#ifdef XY_DOCUMENT_PINNED_REFERENCE
        // Isolate measurement from MSVC's link-dependent implicit style copy.
        // XyInterned allocates new T(v), a non-const object held as const T.
        // This fixture exclusively owns this unique style. Temporarily replace
        // one field, perform no style-table operations until it is restored,
        // and keep all owners alive. Production never mutates canonical keys.
        auto& canonical=const_cast<STSStyle&>(owner->get());
        memcpy(&original,&(canonical.*fields[field]),8);
        if(raw_canonical)memcpy(&(canonical.*fields[field]),&raw[value],8);
#else
        STSStyle canonical=owner->get();memcpy(&original,&(canonical.*fields[field]),8);
        // Raw-input mode calls the production measurement adapter with a
        // detached style, bypassing canonical construction on both sides.
        if(raw_canonical)memcpy(&(canonical.*fields[field]),&raw[value],8);
#endif
        uint64_t measured;memcpy(&measured,&(canonical.*fields[field]),8);
        printf("text-measure-input index=%u bits=%016llx\n",index,static_cast<unsigned long long>(measured));
        for(history=0;history<4;++history){
            CText::TextInfo output={history==3?INT_MAX:5,0x12345678,0x23456789};measure_used=extent_count=0;reset();if(sticky)check(std::feraiseexcept(FE_DIVBYZERO)==0);const Status before=status();
            observing=true;
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
            if(raw_canonical){XyRust::RendererDcOperation operation;FwCMyFont measured_font(canonical,operation);XyRust::MeasureText(output,canonical,text,operation,measured_font.get());}
            else
#endif
            TextProbe::measure(output,*owner,text);
            const Status after=status();observing=false;
            printf("text-measure-case index=%u sticky=%u field=%u bits=%016llx history=%u events=%u output=%d,%d,%d",index++,sticky,field,static_cast<unsigned long long>(raw[value]),history,measure_used,output.m_width,output.m_ascent,output.m_descent);emit("before",before);emit("after",after);printf("\n");
            for(unsigned j=0;j<measure_used;++j){const auto& e=measure_events[j];printf("text-measure-call name=%s count=%d result=%d width=%d",e.name,e.count,e.result,e.width);emit("before",e.before);emit("after",e.after);printf("\n");}
        }
#ifdef XY_DOCUMENT_PINNED_REFERENCE
        if(raw_canonical)memcpy(&(canonical.*fields[field]),&original,8);
#endif
    }
    // NaN canonical keys remain live through process reclamation. Native expired
    // key lookup/destruction is a separate unresolved outcome, never exercised.
    printf("text-measure-complete cases=%u\n",index);fflush(stdout);_exit(0);
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
