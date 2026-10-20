#define main unused_renderer_main
#include "../tools/compat/renderer_dc_native.cpp"
#undef main
#include "xy_bitmap.h"
// Link unchanged pinned drawing/bitmap functions for inspection; never execute.
int main(){
    auto overlay=std::make_shared<Overlay>();DWORD switches[6]={0xff000000,0xffffffff};CRect clip(0,0,8,8);
    std::unique_ptr<XyBitmap> bitmap(XyBitmap::CreateBitmap(clip,XyBitmap::PACK));
    auto volatile draw=&Rasterizer::Draw;auto volatile add=&Rasterizer::AdditionDraw;
    draw(bitmap.get(),overlay,clip,nullptr,0,0,switches,true,false);
    add(bitmap.get(),overlay,clip,nullptr,0,0,switches,true,false);
    return 0;
}
