#define main unused_renderer_main
#include "../tools/compat/renderer_dc_native.cpp"
#undef main
// Link the unchanged operation for inspection only; this program is not run.
int main(){
    auto overlay=std::make_shared<Overlay>();DWORD switches[6]={0xff000000,0xffffffff};CRect dirty;
    auto volatile composite=&Rasterizer::CompositeAlphaMask;
    auto result=composite(overlay,CRect(0,0,8,8),nullptr,0,0,switches,true,false,&dirty);
    return result?0:1;
}
