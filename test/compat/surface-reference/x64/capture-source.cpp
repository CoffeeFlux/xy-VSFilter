#define main unused_renderer_main
#include "../tools/compat/renderer_dc_native.cpp"
#undef main
#include "xy_bitmap.h"
// Link unchanged cleanup output operations for inspection only; never execute.
int main(){
    SubPicDesc spd={};POINT point={};SIZE size={};XyPlannerFormatExtra planes={};
    auto volatile pack=&XyBitmap::AlphaBltPack;auto volatile planar=&XyBitmap::AlphaBltPlannar;
    auto volatile blit=&XyBitmap::BltPack;auto volatile flip=&XyBitmap::FlipAlphaValue;
    auto volatile solid=&Rasterizer::FillSolidRect;
    pack(spd,point,size,nullptr,0);planar(spd,point,size,planes,0);blit(spd,point,size,nullptr,0);
    flip(nullptr,0,0,0);solid(spd,0,0,0,0,0);return 0;
}
