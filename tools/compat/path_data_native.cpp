// SPDX-License-Identifier: GPL-2.0-or-later
// Actual PathData lifecycle, initialized GDI captures and machine-integer edges.
#define main unused_renderer_main
#include "renderer_dc_native.cpp"
#undef main
#include <cache_manager.h>
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include <RustRendererDc.h>
#endif
static HDC path_dc;static unsigned index;
static HDC WINAPI capture_path_dc(HDC dc){path_dc=realCreate(dc);return path_dc;}
static void show(const char* name,const PathData& p){
    const Status observed=status();const ULONG hash=PathDataTraits::Hash(p);
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    const auto snapshot=p.Snapshot();const int count=static_cast<int>(snapshot.points.size());const POINT* points=snapshot.points.data();const BYTE* types=snapshot.types.data();
#else
    const int count=p.mPathPoints;const POINT* points=p.mpPathPoints;const BYTE* types=p.mpPathTypes;
#endif
    check(count>=0&&count<100);
    printf("path-state index=%u name=%s count=%d hash=%08lx",index++,name,count,hash);emit("status",observed);printf("\n");
    for(int i=0;i<count;++i)printf("path-point type=%u x=%ld y=%ld\n",unsigned(types[i]),points[i].x,points[i].y);
}
static void figure(){check(MoveToEx(path_dc,13,-7,nullptr)!=0);check(LineTo(path_dc,31,29)!=0);check(LineTo(path_dc,-5,11)!=0);}
static void geometry(PathData& p){CPoint top;CSize size;p.AlignLeftTop(&top,&size);printf("path-geometry index=%u values=%ld,%ld,%ld,%ld\n",index,top.x,top.y,size.cx,size.cy);}
static void result(bool ok){printf("path-result index=%u value=%u\n",index,ok?1:0);}
static bool scripted;static unsigned pattern;static decltype(&GetPath) get_real;
static int WINAPI get_script(HDC dc,LPPOINT points,LPBYTE types,int count){
    if(!scripted)return get_real(dc,points,types,count);
    const POINT values[][3]={{{13,-7},{31,29},{-5,11}},{{INT_MIN,INT_MAX},{INT_MAX,INT_MIN},{0,0}},{{-1,-65},{64,7},{-8,-8}},{{INT_MAX-7,INT_MAX-6},{INT_MAX-1,INT_MAX},{INT_MAX-8,INT_MAX-9}},{{INT_MIN,INT_MIN+7},{INT_MIN+8,INT_MIN+9},{INT_MIN+63,INT_MIN+64}},{{0,0},{0,0},{0,0}}};
    check(dc==path_dc);if(!points&&!types&&count==0)return 3;check(points&&types&&count==3);
    for(int i=0;i<3;++i){points[i]=values[pattern][i];types[i]=pattern==5?(i==0?255:BYTE(i-1)):(i==0?6:BYTE(i+1));}return 3;
}
static BOOL WINAPI fail_path(HDC dc){check(dc==path_dc);SetLastError(5432);errno=4321;return FALSE;}
int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE);CCritSec lock;
    Import capture("CreateCompatibleDC",capture_path_dc,realCreate);CRenderedTextSubtitle renderer(&lock);
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    XyRust::RendererDcOperation operation;check(path_dc==operation.get());
#endif
    printf("path-storage pointer_bits=%u\n",unsigned(sizeof(void*)*8));
    PathData empty,source;reset();show("empty",empty);
    check(source.BeginPath(path_dc));figure();check(source.EndPath(path_dc));show("source",source);
    PathData copy(source),empty_copy(empty);show("copy",copy);show("empty-copy",empty_copy);
    check(copy==source);copy=empty;show("assign-empty",copy);check(copy==source);
    copy=copy;show("self-assign",copy);check(copy==source);
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    {auto edit=source.Snapshot();edit.points[0].x+=10;source.Replace(edit);}
#else
    source.mpPathPoints[0].x+=10;
#endif
    show("source-mutated",source);show("copy-independent",copy);check(!(copy==source));
    copy=source;show("assign-same-count",copy);check(copy==source);
    check(source.PartialBeginPath(path_dc,false));figure();check(source.PartialEndPath(path_dc,100,-200));show("append",source);
    copy=source;show("assign-different-count",copy);check(copy==source);
    check(source.PartialBeginPath(path_dc,false));check(source.PartialEndPath(path_dc,3,4));show("append-empty",source);check(copy==source);
    check(source.BeginPath(path_dc));show("begin-clears",source);check(source.EndPath(path_dc));show("end-empty",source);
    copy=source;show("assign-empty-again",copy);check(!(copy==source));
    copy._TrashPath();show("clear",copy);copy._TrashPath();show("clear-again",copy);
    {Import scripted_get("GetPath",get_script,get_real);scripted=true;
        for(pattern=0;pattern<6;++pattern){PathData value;reset();check(value.BeginPath(path_dc));figure();check(value.EndPath(path_dc));show("raw",value);
            geometry(value);show("aligned",value);
            check(value.PartialBeginPath(path_dc,false));figure();check(value.PartialEndPath(path_dc,pattern%2?INT_MAX:1,pattern%2?INT_MIN:-1));show("translated",value);
        }
        scripted=false;
    }
    check(copy.BeginPath(path_dc));figure();check(copy.EndPath(path_dc));
    {decltype(&BeginPath) real;Import fail("BeginPath",fail_path,real);reset();result(copy.BeginPath(path_dc));show("failed-begin",copy);}
    {decltype(&EndPath) real;Import fail("EndPath",fail_path,real);reset();check(copy.BeginPath(path_dc));figure();result(copy.EndPath(path_dc));show("failed-end",copy);}
    {decltype(&CloseFigure) real;Import fail("CloseFigure",fail_path,real);reset();check(copy.BeginPath(path_dc));figure();result(copy.EndPath(path_dc));show("ignored-close",copy);}
    copy._TrashPath();geometry(copy);show("aligned-empty",copy);
    printf("path-storage-complete cases=%u\n",index);return 0;
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 2;}}
