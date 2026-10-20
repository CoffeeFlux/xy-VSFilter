#define main unused_renderer_main
#include "../tools/compat/renderer_dc_native.cpp"
#undef main
int main(){volatile float be=0.25f;volatile double gaussian=0.5;return Rasterizer::IsItReallyBlur(be,gaussian)?0:1;}
