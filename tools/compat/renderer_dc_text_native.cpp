// SPDX-License-Identifier: GPL-2.0-or-later
// Actual CText methods and PathData, with real GDI handles and bounded failures.
#define main unused_dc_lifecycle_main
#include "renderer_dc_native.cpp"
#undef main
#include <vector>

static HDC text_dc;static HGDIOBJ stock_font;
static HDC WINAPI captureCreate(HDC dc){text_dc=realCreate(dc);return text_dc;}
struct TextEvent {const char* name;int a,b,result,x,y;Status before,after;};
static TextEvent text_events[128];static unsigned text_used;static bool text_recording;
static TextEvent* text_begin(const char* name,HDC dc,int a=0,int b=0){
    if(!text_recording)return nullptr;check(dc==text_dc&&text_used<128);auto e=&text_events[text_used++];*e=TextEvent{};
    e->name=name;e->a=a;e->b=b;e->before=status();return e;
}
static void text_end(TextEvent* e,int result){if(e){e->result=result;e->after=status();}}
static decltype(&SelectObject) text_real_select;
static decltype(&GetTextExtentPoint32W) text_real_extent;
static decltype(&TextOutW) text_real_out;
static decltype(&BeginPath) text_real_begin;
static decltype(&CloseFigure) text_real_close;
static decltype(&EndPath) text_real_end;
static decltype(&AbortPath) text_real_abort;
static decltype(&GetPath) text_real_get;
static void fail(){SetLastError(2345+mode);errno=3456+mode;}
static HGDIOBJ WINAPI text_select(HDC dc,HGDIOBJ object){auto e=text_begin("select",dc,object==stock_font);auto r=text_real_select(dc,object);text_end(e,(r?1:0)|(r==stock_font?2:0)|(r==object?4:0));return r;}
static BOOL WINAPI text_extent(HDC dc,LPCWSTR s,int count,LPSIZE size){auto e=text_begin("extent",dc,count);BOOL r;if(text_recording&&mode==1){fail();r=FALSE;}else r=text_real_extent(dc,s,count,size);text_end(e,r);if(e&&r){e->x=size->cx;e->y=size->cy;}return r;}
static BOOL WINAPI text_out(HDC dc,int x,int y,LPCWSTR s,int count){auto e=text_begin("text",dc,count);BOOL r;if(text_recording&&mode==3){fail();r=FALSE;}else r=text_real_out(dc,x,y,s,count);text_end(e,r);return r;}
static BOOL WINAPI text_begin_path(HDC dc){auto e=text_begin("begin",dc);BOOL r;if(text_recording&&mode==2){fail();r=FALSE;}else r=text_real_begin(dc);text_end(e,r);return r;}
static BOOL WINAPI text_close(HDC dc){auto e=text_begin("close",dc);auto r=text_real_close(dc);text_end(e,r);return r;}
static BOOL WINAPI text_end_path(HDC dc){auto e=text_begin("end",dc);BOOL r;if(text_recording&&mode==4){fail();r=FALSE;}else r=text_real_end(dc);text_end(e,r);return r;}
static BOOL WINAPI text_abort(HDC dc){auto e=text_begin("abort",dc);auto r=text_real_abort(dc);text_end(e,r);return r;}
static int WINAPI text_get(HDC dc,LPPOINT points,LPBYTE types,int count){auto e=text_begin("get",dc,count,(points?1:0)|(types?2:0));auto r=text_real_get(dc,points,types,count);text_end(e,r);return r;}
class TextProbe:public CText {
public:
    TextProbe(const FwSTSStyle& s,const CStringW& text):CText(s,text,0,0,0){}
    bool path(PathData& output){return CreatePath(&output);}
    static void measure(TextInfo& out,const FwSTSStyle& s,const CStringW& text){GetTextInfo(&out,s,text);}
};
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);CCritSec lock;
    Import a("CreateCompatibleDC",captureCreate,realCreate),b("SelectObject",text_select,text_real_select),c("GetTextExtentPoint32W",text_extent,text_real_extent),
        d("TextOutW",text_out,text_real_out),e("BeginPath",text_begin_path,text_real_begin),f("CloseFigure",text_close,text_real_close),
        g("EndPath",text_end_path,text_real_end),h("AbortPath",text_abort,text_real_abort),i("GetPath",text_get,text_real_get);
    const wchar_t raw[]={L'A',0,L'g'};const wchar_t surrogate[]={0xd800,0};
    const std::vector<CStringW> strings={CStringW(L"Ag"),CStringW(raw,3),CStringW(surrogate),CStringW(L"")};
    printf("dc-text pointer_bits=%u\n",unsigned(sizeof(void*)*8));unsigned index=0;
    for(unsigned spacing=0;spacing<2;++spacing)for(unsigned text=0;text<strings.size();++text)for(mode=0;mode<5;++mode){
        CRenderedTextSubtitle renderer(&lock);check(text_dc!=NULL);stock_font=GetCurrentObject(text_dc,OBJ_FONT);
        STSStyle style;style.fontName=L"Arial";style.fontSize=20;style.fontSpacing=spacing?1.25:0;FwSTSStyle interned(style);
        TextProbe word(interned,strings[text]);PathData path;TextProbe::TextInfo info={0,0,0};
        text_used=0;reset();text_recording=true;TextProbe::measure(info,interned,strings[text]);Status measured=status();unsigned measurement_events=text_used;
        reset();bool result=word.path(path);Status pathed=status();text_recording=false;
        check(path.mPathPoints>=0&&path.mPathPoints<100000);const auto selected=GetCurrentObject(text_dc,OBJ_FONT);
        printf("dc-text-case index=%u spacing=%u text=%u mode=%u events=%u measurement_events=%u result=%u measured=%d,%d,%d selected_stock=%u selected_null=%u points=%d",index++,spacing,text,mode,text_used,measurement_events,result?1:0,info.m_width,info.m_ascent,info.m_descent,selected==stock_font,selected==NULL,path.mPathPoints);emit("measure_status",measured);emit("path_status",pathed);printf("\n");
        printf("dc-text-path");for(int j=0;j<path.mPathPoints;++j)printf(" %02x:%ld,%ld",unsigned(path.mpPathTypes[j]),path.mpPathPoints[j].x,path.mpPathPoints[j].y);printf("\n");
        for(unsigned j=0;j<text_used;++j){const auto& v=text_events[j];printf("dc-text-call name=%s a=%d b=%d result=%d extent=%d,%d",v.name,v.a,v.b,v.result,v.x,v.y);emit("before",v.before);emit("after",v.after);printf("\n");}
        // End an ignored partial-path failure only after observation, then let
        // the real renderer destructor release its DC at the original point.
        text_real_abort(text_dc);
    }
    printf("dc-text-complete cases=%u\n",index);return 0;
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
