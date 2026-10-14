// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_rle_storage.h"
static uint64_t calls=0,fields=0,initializedBytes=0,unwrittenCells=0,copies=0,partialCopyExclusions=0,observations=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
struct Effects {int error,flags;unsigned long last;Effects():error(errno),flags(std::fetestexcept(FE_ALL_EXCEPT)),last(0){
#ifdef _WIN32
last=GetLastError();
#endif
}bool operator==(const Effects& other)const{return error==other.error&&flags==other.flags&&last==other.last;}};
template<class Object> static std::vector<uint64_t> snapshot(Object& object,bool count){
    std::vector<uint64_t> result={static_cast<uint64_t>(object.GetRLEDataSize()),static_cast<uint64_t>(object.m_nRLEPos),object.IsRLEComplete()?1u:0u,object.GetRLEData()?1u:0u};
    if(object.GetRLEData()){
        const auto& region=rleBaseRegion(object.GetRLEData());
        requireRle(object.GetRLEDataSize()>=0&&static_cast<size_t>(object.GetRLEDataSize())<=region.count);rleGuards(region);
        for(size_t i=0;i<region.count;++i){result.push_back(region.initialized[i]);if(region.initialized[i]){result.push_back(region.data[i]);initializedBytes+=count;}else unwrittenCells+=count;}
    }
    result.push_back(object.m_Palette.GetCount());
    for(const auto& color:object.m_Palette.data)result.insert(result.end(),{color.entry_id,color.Y,color.Cr,color.Cb,color.T});
    result.insert(result.end(),object.m_Colors,object.m_Colors+256);
    result.insert(result.end(),{static_cast<uint64_t>(object.m_colorType),static_cast<uint64_t>(object.m_OriginalColorType),static_cast<uint64_t>(object.m_OriginalYuvRangeType)});
    return result;
}
template<class A,class B,class Operation> static void step(A& a,B& b,Operation operation){
    rleEvents.clear();seed();operation(a);Effects ea;const auto trace=rleEvents;const auto sa=snapshot(a,true);
    rleEvents.clear();seed();operation(b);Effects eb;const auto sb=snapshot(b,false);
    if(trace!=rleEvents||sa!=sb||!(ea==eb)){fprintf(stderr,"RLE storage mismatch at call %llu\n",(unsigned long long)calls);std::abort();}
    ++calls;fields+=sa.size();observations+=trace.size();
}
template<class Object> static void palette(Object& object,unsigned salt){
    for(unsigned i=0;i<256;++i)requireRle(object.m_Colors[i]==0);
    object.m_Palette.SetCount(salt%17);for(int i=0;i<object.m_Palette.GetCount();++i)object.m_Palette[i]={static_cast<BYTE>(i),static_cast<BYTE>(salt),static_cast<BYTE>(i*7),static_cast<BYTE>(i*11),static_cast<BYTE>(255-i)};
    for(unsigned i=0;i<256;++i)object.m_Colors[i]=i*0x0735ab19u+salt;
    object.m_colorType=static_cast<int>(salt%9)-1;object.m_OriginalColorType=salt%5;object.m_OriginalYuvRangeType=salt%3;
}
template<class A,class B>static void copyComplete(A& a,B& b){
    if(!a.IsRLEComplete()){++partialCopyExclusions;return;}
    rleEvents.clear();seed();A* ca=a.Copy();Effects ea;const auto trace=rleEvents;
    rleEvents.clear();seed();B* cb=b.Copy();Effects eb;
    requireRle(trace==rleEvents&&ea==eb&&snapshot(*ca,true)==snapshot(*cb,false));
    requireRle(snapshot(a,false)==snapshot(*ca,false));++copies;observations+=trace.size();
    const auto saved=snapshot(*ca,false);const auto source=snapshot(a,false);
    rleEvents.clear();delete ca;const auto released=rleEvents;rleEvents.clear();delete cb;requireRle(released==rleEvents&&source==snapshot(a,false));
    requireRle(!saved.empty());
}
struct RleOperation {
    bool set,alias;const BYTE* input;int size,total;
    template<class Object>void operator()(Object& object)const {
        if(set)object.SetRLEData(input,size,total);
        else object.AppendRLEData(alias?object.GetRLEData():input,size<0?total-object.m_nRLEPos:size);
    }
};
static void history(int total,int first,unsigned salt,bool decline){
    requireRle(rleRegions.empty());rleDecline=decline;
    std::vector<BYTE> input(8192);for(size_t i=0;i<input.size();++i)input[i]=static_cast<BYTE>(i*37+salt);
    rleRegions.push_back({input.data(),input.size(),NULL,NULL,std::vector<bool>(input.size(),true)});
    auto* a=new ReferenceRle::CompositionObject();auto* b=new CandidateRle::CompositionObject();palette(*a,salt);palette(*b,salt);
    step(*a,*b,RleOperation{true,false,input.data(),first,total});copyComplete(*a,*b);
    // Rejected oversized appends leave both the initialized prefix and position
    // intact; ASSERT is recorded as a diagnostic callback, not a native trap.
    step(*a,*b,RleOperation{false,false,input.data()+17,total-first+1,0});
    for(int chunk:{0,1,3,7,31,127,1024}){
        const int remaining=total-a->m_nRLEPos;const int size=std::min(chunk,remaining);
        const bool alias=size>0&&size<=a->m_nRLEPos&&(salt&1);
        step(*a,*b,RleOperation{false,alias,input.data()+31,size,0});copyComplete(*a,*b);
    }
    step(*a,*b,RleOperation{false,false,input.data()+73,-1,total});copyComplete(*a,*b);
    // Replacement and zero-sized native allocations exercise ownership changes.
    for(int size:{17,0,33,1}){step(*a,*b,RleOperation{true,false,input.data()+11,size,size});copyComplete(*a,*b);}
    rleEvents.clear();delete a;const auto trace=rleEvents;rleEvents.clear();delete b;requireRle(trace==rleEvents);
    requireRle(rleRegions.size()==1&&rleRegions[0].data==input.data());rleRegions.clear();
}
static void adjacentRegionControl(){
    // Deterministically model adjacent addresses, independently of the host
    // allocator. The empty owner starts exactly at another region's end.
    BYTE storage[64]={};
    requireRle(rleRegions.empty());
    rleRegions.push_back({storage,16,NULL,NULL,std::vector<bool>(16,true)});
    rleRegions.push_back({storage+16,0,NULL,NULL,{}});
    rleRegions.push_back({storage+32,16,NULL,NULL,std::vector<bool>(16,true)});
    ReferenceRle::CompositionObject object;
    palette(object,0);
    object.m_pRLEData=storage+16;
    const auto empty=snapshot(object,false);
    // Four RLE fields, palette count, 256 colors and three color metadata fields.
    requireRle(empty.size()==264);
    rleEvents.clear();
    rleCopy(storage+16,storage+48,0);
    requireRle(rleEvents==std::vector<int64_t>({3,0,0,0}));
    // Nonempty copies must still retain their actual offsets and data checks.
    rleEvents.clear();
    rleCopy(storage+33,storage+2,3);
    requireRle(rleEvents==std::vector<int64_t>({3,1,2,3}));
    object.m_pRLEData=NULL;
    rleRegions.clear();rleEvents.clear();
}
int main(){
    adjacentRegionControl();
    for(int total:{0,1,2,3,7,15,16,17,31,32,33,127,255,256,257,1024})
        for(int first=0;first<=total;first+=std::max(1,total/17))for(unsigned salt:{0u,1u,17u,255u})for(bool decline:{false,true})history(total,first,salt,decline);
    for(int first:{0,1,15,16,17,127,2047,4096})history(4096,first,first,false);
    requireRle(calls&&copies&&partialCopyExclusions&&unwrittenCells&&rleOwners&&rleOwners==rleLoans&&rleLoans==rleReleases&&rleDeclines&&rleAssertions&&rleRegions.empty());
    printf("%llu exact RLE storage mutation calls passed (%llu state fields / %llu initialized bytes / %llu unwritten cells tracked without reads / %llu logical allocation-copy observations / %llu complete deep copies / %llu Rust owners, loans and releases / %llu modeled Rust declines to native allocation; %llu incomplete-copy fixtures excluded, %llu modeled assertion diagnostics; whole pinned Set/Append/destructor/Copy and accessors, chunking, bounded disjoint source aliases, reuse, zero-sized allocation, palette state and errno/FP/Windows error state; partial-tail consumption/copy, source replacement aliasing, negative or overflowing lengths and actual OOM excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)initializedBytes,(unsigned long long)unwrittenCells,(unsigned long long)observations,(unsigned long long)copies,(unsigned long long)rleOwners,(unsigned long long)rleDeclines,(unsigned long long)partialCopyExclusions,(unsigned long long)rleAssertions);
}
