// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_bitmap_storage.h"
static uint64_t calls=0,fields=0,initialized=0,unwritten=0,accessors=0,failures=0,zeroAllocations=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
static void effects(std::vector<uint64_t>& out){const int error=errno,flags=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef _WIN32
const unsigned long last=GetLastError();
#else
const unsigned long last=0;
#endif
out.insert(out.end(),{static_cast<uint64_t>(error),static_cast<uint64_t>(flags),last});}
static uint64_t pointerOffset(LPCVOID value,LPCVOID base,size_t count){
    if(!value)return 0;
    if(value==reinterpret_cast<LPCVOID>(uintptr_t(1)))return UINT64_MAX;
    const uintptr_t p=reinterpret_cast<uintptr_t>(value),b=reinterpret_cast<uintptr_t>(base);
    bitmapRequire(base&&p>=b&&p-b<=count);return p-b+1;
}
template<class Bitmap>static void snapshot(const Bitmap& image,std::vector<uint64_t>& out,bool count){
    out.insert(out.end(),{static_cast<uint64_t>(image.type),static_cast<uint64_t>(image.x),static_cast<uint64_t>(image.y),static_cast<uint64_t>(image.w),static_cast<uint64_t>(image.h),static_cast<uint64_t>(image.pitch),g_xy_malloc_used_size});
    // bpp and unwritten packed row padding are intentionally not read.
    if(!image.bits){out.push_back(0);return;}
    const auto& region=bitmapBase(image.bits);bitmapGuards(region);
    out.insert(out.end(),{1,region.count,reinterpret_cast<uintptr_t>(image.bits)&15});
    for(const auto p:image.plans)out.push_back(pointerOffset(p,image.bits,region.count));
    for(size_t i=0;i<region.count;++i){out.push_back(region.initialized[i]);if(region.initialized[i]){out.push_back(region.data[i]);initialized+=count;}else unwritten+=count;}
}
struct Outcome {std::vector<uint64_t> values,events;bool operator==(const Outcome& other)const{return values==other.values&&events==other.events;}};
template<class Bitmap,class Frame>static Outcome execute(int x,int w,int h,int layout,bool decline,bool fail,bool count){
    bitmapRequire(bitmapRegions.empty()&&!g_xy_malloc_used_size);bitmapEvents.clear();bitmapAssertions=0;bitmapDecline=decline;bitmapObjectFail=fail;
    Outcome result;auto& out=result.values;seed();
    std::shared_ptr<Bitmap> image(Bitmap::CreateBitmap(CRect(x,-17,x+w,h-17),static_cast<typename Bitmap::MemLayout>(layout)));effects(out);
    out.push_back(bitmapAssertions);out.push_back(image?1:0);
    if(!image){result.events=bitmapEvents;failures+=count;return result;}
    snapshot(*image,out,count);zeroAllocations+=count&&image->bits&&bitmapBase(image->bits).count==0;
    if(image->bits){auto& region=bitmapBase(image->bits);for(size_t i=0;i<region.count;i+=3){region.data[i]=static_cast<uint8_t>(i*71+29);region.initialized[i]=true;}}
    snapshot(*image,out,count);seed();Bitmap::ClearBitmap(image.get());effects(out);snapshot(*image,out,count);
    Frame frame;frame.m_output_rect=CRect(-91,73,400,500);frame.m_bitmaps.values.push_back(image);frame.m_bitmap_ids.values.push_back(-17);
    const size_t extent=image->bits?bitmapBase(image->bits).count:0;
    for(int index:{-1,0,1})for(unsigned mask=0;mask<32;++mask){
        ULONGLONG id=12345;POINT position={17,29};SIZE size={31,37};LPCVOID pixels=reinterpret_cast<LPCVOID>(uintptr_t(1));int pitch=-101;
        seed();const HRESULT status=frame.GetBitmap(index,mask&1?&id:NULL,mask&2?&position:NULL,mask&4?&size:NULL,mask&8?&pixels:NULL,mask&16?&pitch:NULL);effects(out);
        out.insert(out.end(),{static_cast<uint32_t>(status),id,static_cast<uint64_t>(position.x),static_cast<uint64_t>(position.y),static_cast<uint64_t>(size.cx),static_cast<uint64_t>(size.cy),pointerOffset(pixels,image->bits,extent),static_cast<uint64_t>(pitch)});accessors+=count;
    }
    for(auto color:{XY_CS_ARGB,XY_CS_ARGB_F,XY_CS_AYUV_PLANAR})for(int index:{-1,0,1})for(bool output:{false,true}){
        frame.m_xy_color_space=color;XyPlannerFormatExtra extra;for(auto& p:extra.plans)p=reinterpret_cast<LPCVOID>(uintptr_t(1));
        seed();const HRESULT status=frame.GetBitmapExtra(index,output?&extra:NULL);effects(out);out.push_back(static_cast<uint32_t>(status));
        for(auto p:extra.plans)out.push_back(pointerOffset(p,image->bits,extent));accessors+=count;
    }
    auto alias=image;image.reset();snapshot(*alias,out,count);frame.m_bitmaps.values.clear();snapshot(*alias,out,count);
    seed();alias.reset();effects(out);out.push_back(g_xy_malloc_used_size);bitmapRequire(bitmapRegions.empty()&&!g_xy_malloc_used_size);
    result.events=bitmapEvents;return result;
}
static void compare(int x,int w,int h,int layout,bool decline=false,bool fail=false){
    const auto a=execute<ReferenceBitmap::XyBitmap,ReferenceBitmap::XySubRenderFrame>(x,w,h,layout,decline,fail,true);
    const auto b=execute<CandidateBitmap::XyBitmap,CandidateBitmap::XySubRenderFrame>(x,w,h,layout,decline,fail,false);
    if(!(a==b)){fprintf(stderr,"Bitmap mismatch x=%d size=%d,%d layout=%d decline=%d objectFail=%d\n",x,w,h,layout,decline,fail);std::abort();}
    ++calls;fields+=a.values.size()+a.events.size();
}
int main(){
    for(int x=-17;x<17;++x)for(int w:{0,1,3,15,16,17,31})for(int h:{0,1,3,9})for(int layout:{0,1})for(bool decline:{false,true})compare(x,w,h,layout,decline);
    for(int x:{-17,-1,0,1,15,16})for(int layout:{0,1})for(bool decline:{false,true})compare(x,17,3,layout,decline,true);
    // Unsupported layout with no rows preserves ASSERT and null-buffer cleanup.
    compare(-1,0,0,2);
    if(!calls||!initialized||!unwritten||!accessors||!failures||!zeroAllocations||!bitmapOwners||bitmapOwners!=bitmapLoans||bitmapOwners!=bitmapReleases||!bitmapDeclines)std::abort();
    printf("%llu exact bitmap histories passed (%llu state/initialization/accessor/allocation fields / %llu initialized bytes / %llu unwritten cells tracked without reads / %llu GetBitmap/GetBitmapExtra calls / %llu modeled null object allocations / %llu zero-byte native allocations / %llu Rust owners, loans and releases / %llu modeled Rust declines; whole pinned CreateBitmap/ClearBitmap/destructor and accessors, all alignment residues, packed/planar plane offsets, partial padding writes retained across clears, optional outputs and invalid indices, shared-frame and last-alias lifetime, logical cache accounting and errno/FP/Windows error state; allocation/clear primitives controlled, uninitialized bpp untouched, native pixel-allocation failure, invalid/overflowing geometry and actual COM host lifecycle excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)initialized,(unsigned long long)unwritten,(unsigned long long)accessors,(unsigned long long)failures,(unsigned long long)zeroAllocations,(unsigned long long)bitmapOwners,(unsigned long long)bitmapDeclines);
}
