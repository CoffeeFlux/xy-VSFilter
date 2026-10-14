// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_ellipse_storage.h"
static uint64_t histories=0,queries=0,intersections=0,matrixFields=0,owned=0,negativeOffsets=0,invalidInits=0,matrixOwners=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
struct Effects {int error,flags;unsigned long last;Effects():error(errno),flags(std::fetestexcept(FE_ALL_EXCEPT)),last(0){
#ifdef _WIN32
last=GetLastError();
#endif
}bool operator==(const Effects& o)const{return error==o.error&&flags==o.flags&&last==o.last;}};
static void fail(const char* what,int rx,int ry){fprintf(stderr,"Ellipse owner mismatch %s rx=%d ry=%d\n",what,rx,ry);std::abort();}
template<class T> static bool sameBits(const T& a,const T& b){return std::memcmp(&a,&b,sizeof(T))==0;}
static void compare(int rx,int ry){
    ReferenceEllipseStorage::XyEllipse a;CandidateEllipseStorage::XyEllipse b;
    seed();int ra=a.init(rx,ry);Effects ea;seed();int rb=b.init(rx,ry);Effects eb;
    if(ra!=rb||ra!=0||!(ea==eb)){fprintf(stderr,"init results %d/%d errno %d/%d flags %d/%d last %lu/%lu\n",ra,rb,ea.error,eb.error,ea.flags,eb.flags,ea.last,eb.last);fail("init",rx,ry);}
    if(!b.m_arc.Ready()||b.m_left_arc||b.m_left_arc_base)fail("owner selection",rx,ry);++owned;
    if(a.m_rx!=b.m_rx||a.m_ry!=b.m_ry||!sameBits(a.m_f_rx,b.m_f_rx)||!sameBits(a.m_f_ry,b.m_f_ry)||!sameBits(a.m_f_half_inv_rx,b.m_f_half_inv_rx)||!sameBits(a.m_f_half_inv_ry,b.m_f_half_inv_ry))fail("native float fields",rx,ry);
    if((a.m_cross_matrix!=NULL)!=(b.m_matrix.Ready()||b.m_cross_matrix!=NULL))fail("matrix selection",rx,ry);
    if(b.m_matrix.Ready()){if(b.m_cross_matrix||b.m_cross_matrix_base)fail("matrix owner selection",rx,ry);++matrixOwners;}
    if(a.m_cross_matrix)for(int dy=-2*ry;dy<0;++dy)for(int dx=-rx;dx<=rx;++dx){if(a.m_cross_matrix[dy*(2*rx+1)+dx]!=(b.m_matrix.Ready()?b.m_matrix.Get(dy,dx):b.m_cross_matrix[dy*(2*rx+1)+dx]))fail("native matrix",rx,ry);++matrixFields;}
    for(int round=0;round<3;++round)for(int y=-ry;y<=ry;++y){
        seed();int al=a.get_left_arc_x(y),ar=a.get_right_arc_x(y);Effects ae;
        seed();int bl=b.get_left_arc_x(y),br=b.get_right_arc_x(y);Effects be;
        if(al!=bl||ar!=br||!(ae==be))fail("centered query",rx,ry);queries+=2;
        uint64_t center=(uint64_t(ry+200)<<32)|uint32_t(round==2?UINT32_MAX-100:400);
        if(y>=0){
            seed();al=a.get_left_arc_x(center,ry+200+y);ar=a.get_right_arc_x(center,ry+200+y);Effects ace;
            seed();bl=b.get_left_arc_x(center,ry+200+y);br=b.get_right_arc_x(center,ry+200+y);Effects bce;
            if(al!=bl||ar!=br||!(ace==bce))fail("translated query",rx,ry);queries+=2;
        }else{
            // Keep the reference read as a valid centered signed index. The
            // full native unsigned pointer-index expression is left to DLL CI.
            int left=static_cast<int>(XY_POINT_X(center)+a.get_left_arc_x(y));
            int right=static_cast<int>(XY_POINT_X(center)-a.get_left_arc_x(y));
            if(left!=b.get_left_arc_x(center,ry+200+y)||right!=b.get_right_arc_x(center,ry+200+y))fail("negative translated index",rx,ry);negativeOffsets+=2;
        }
    }
    for(int dy=1;dy<=2*ry;dy+=(ry>16?ry/8:1))for(int dx=-rx-1;dx<=rx+1;dx+=(rx>16?rx/8:1)){
        uint64_t c1=(uint64_t(ry+200)<<32)|uint32_t(rx+300+dx),base=(uint64_t(ry+200+dy)<<32)|uint32_t(rx+300);
        seed();int al=a.cross_left(c1,base),ar=a.cross_right(c1,base);Effects ae;
        seed();int bl=b.cross_left(c1,base),br=b.cross_right(c1,base);Effects be;
        if(al!=bl||ar!=br||!(ae==be))fail("intersection",rx,ry);intersections+=2;
    }
    // Rejected initialization must leave the already constructed table usable.
    for(auto radii: {std::pair<int,int>(-1,ry),{rx,0},{rx,-1}}){
        seed();int al=a.init(radii.first,radii.second);Effects ae;seed();int bl=b.init(radii.first,radii.second);Effects be;
        if(al!=bl||al!=-1||!(ae==be)||a.get_left_arc_x(0)!=b.get_left_arc_x(0))fail("rejected init",rx,ry);++invalidInits;
    }
    ++histories;
}
int main(){
    for(int rx=0;rx<=64;++rx)for(int ry=1;ry<=64;++ry)compare(rx,ry);
    for(int rx:{0,1,2,15,63,64,65,127,128,129,255})for(int ry:{1,2,15,63,64,65,127,128,129,255})compare(rx,ry);
    if(!ellipseScratchOwners||!ellipseScratchReads||!ellipseScratchWrites||ellipseScratchFallbacks)std::abort();
    printf("%llu Rust ellipse scratch owners / %llu reads / %llu writes / %llu native fallbacks passed (native float computation, zero initialization and row retention preserved)\n",(unsigned long long)ellipseScratchOwners,(unsigned long long)ellipseScratchReads,(unsigned long long)ellipseScratchWrites,(unsigned long long)ellipseScratchFallbacks);
    if(!matrixOwners||!owned||!matrixFields||!intersections||!negativeOffsets||!invalidInits)std::abort();
    printf("%llu exact persistent ellipse histories passed (%llu Rust arc owners / %llu Rust matrix owners / %llu direct and translated arc queries / %llu intersection returns / %llu initialized native matrix fields / %llu candidate negative-offset checks against direct native indices / %llu rejected reinitializations; full native float fields and calculations retained, errno/FP/Windows error state under FP-observable compilation; ordinary optimized x64 ambient-flag mismatch recorded separately; valid initialized indices and successful allocation, negative unsigned native pointer-expression parity left to renderer CI, uninitialized matrix row excluded)\n",(unsigned long long)histories,(unsigned long long)owned,(unsigned long long)matrixOwners,(unsigned long long)queries,(unsigned long long)intersections,(unsigned long long)matrixFields,(unsigned long long)negativeOffsets,(unsigned long long)invalidInits);
}
