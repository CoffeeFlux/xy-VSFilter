#define main unused_renderer_main
#include "../tools/compat/renderer_dc_native.cpp"
#undef main
#include <cache_manager.h>
#include <xy_clipper_paint_machine.h>
// Link the unchanged reference paint functions for disassembly. Not executed.
int main(){
    auto clip=std::make_shared<CClipper>(L"m 0 0 l 64 0 64 64 0 64",CSize(64,64),1.0,1.0,false);
    Effect effect;effect.param[2]=8;clip->SetEffect(effect,EF_BANNER);
    CClipperPaintMachine machine(clip);SharedPtrGrayImage2 image;machine.Paint(&image);
    return image?0:1;
}
