// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_vobsub_storage.h"
static uint64_t calls=0,fields=0,observations=0,initialized=0,unwritten=0,failures=0,reuses=0,negativeReuses=0,excluded=0;
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
template<class Object>static std::vector<uint64_t> snapshot(const Object& object,bool count){
    std::vector<uint64_t> out={static_cast<uint64_t>(object.org.cx),static_cast<uint64_t>(object.org.cy),object.lpTemp1?1u:0u,object.lpTemp2?1u:0u,object.lpPixels==nullptr?0u:object.lpPixels==object.lpTemp1?1u:object.lpPixels==object.lpTemp2?2u:3u,
        object.nPlane,object.fCustomPal,object.fAligned,static_cast<uint64_t>(object.tridx),object.orgpal?1u:0u,object.cuspal?1u:0u,object.iLang,object.iIdx,object.fForced,object.fAnimated,static_cast<uint64_t>(object.tCurrent),static_cast<uint64_t>(object.start),static_cast<uint64_t>(object.delay),static_cast<uint64_t>(object.rect.left),static_cast<uint64_t>(object.rect.top),static_cast<uint64_t>(object.rect.right),static_cast<uint64_t>(object.rect.bottom)};
    for(const auto& palette:object.pal){out.push_back(palette.pal);out.push_back(palette.tr);}
    // nOffset is uninitialized in the native constructor and deliberately not read.
    for(const auto* pixels:{object.lpTemp1,object.lpTemp2}){
        if(!pixels){out.push_back(0);continue;}
        const auto& region=rleBaseRegion(pixels);rleGuards(region);out.push_back(region.count);
        for(size_t i=0;i<region.count;++i){out.push_back(region.initialized[i]);if(region.initialized[i]){out.push_back(region.data[i]);initialized+=count;}else unwritten+=count;}
    }
    return out;
}
template<class Object>static void stamp(Object& object,unsigned salt){
    for(auto* pixels:{object.lpTemp1,object.lpTemp2})if(pixels){auto& region=rleBaseRegion(pixels);
        for(size_t i=salt%4;i<region.count;i+=4){region.data[i]=static_cast<BYTE>(i*37+salt);region.initialized[i]=true;}
    }
    object.lpPixels=object.lpTemp2;
}
struct History {
    ReferenceVobStorage::CVobSubImage* a;CandidateVobStorage::CVobSubImage* b;int mode;unsigned operation=0;
    explicit History(int allocationMode):mode(allocationMode){requireRle(rleRegions.empty());a=new ReferenceVobStorage::CVobSubImage();b=new CandidateVobStorage::CVobSubImage();requireRle(snapshot(*a,false)==snapshot(*b,false));}
    ~History(){rleEvents.clear();delete a;const auto trace=rleEvents;rleEvents.clear();delete b;requireRle(trace==rleEvents&&rleRegions.empty());observations+=trace.size();}
    void allocate(int width,int height,int failure){
        const int64_t count=int64_t(width)*height,padded=int64_t(width+2)*(height+2);
        const bool resize=!a->lpTemp1||count>int64_t(a->org.cx)*a->org.cy||padded>int64_t(a->org.cx+2)*(a->org.cy+2);
        if(resize&&(count<0||padded<0)){++excluded;return;}
        requireRle(count>=INT32_MIN&&count<=INT32_MAX&&padded>=INT32_MIN&&padded<=INT32_MAX);
        rleDecline=mode==1||(mode==2&&operation%2);++operation;vobFailure=failure;
        auto* oldA=a->lpTemp1;auto* oldB=b->lpTemp1;const auto before=snapshot(*a,false);
        rleEvents.clear();vobAttempt=0;seed();const bool ra=a->Alloc(width,height);Effects ea;const auto trace=rleEvents;const auto sa=snapshot(*a,true);const int attempts=vobAttempt;
        rleEvents.clear();vobAttempt=0;seed();const bool rb=b->Alloc(width,height);Effects eb;
        if(ra!=rb||!(ea==eb)||trace!=rleEvents||sa!=snapshot(*b,false)||attempts!=vobAttempt){fprintf(stderr,"VobSub storage mismatch size=%d,%d failure=%d mode=%d\n",width,height,failure,mode);std::abort();}
        ++calls;fields+=sa.size();observations+=trace.size();failures+=!ra;
        if(!resize){requireRle(oldA==a->lpTemp1&&oldB==b->lpTemp1&&ra);++reuses;negativeReuses+=width<0||height<0;}
        if(ra){stamp(*a,operation);stamp(*b,operation);requireRle(snapshot(*a,false)==snapshot(*b,false));}
        requireRle(!before.empty());
    }
    void clear(){const CSize old=a->org;rleEvents.clear();a->Free();const auto trace=rleEvents;rleEvents.clear();b->Free();requireRle(trace==rleEvents&&snapshot(*a,false)==snapshot(*b,false)&&a->org.cx==old.cx&&a->org.cy==old.cy);}
};
int main(){
    BYTE adjacent[32];
    rleRegions.push_back({adjacent,16,NULL,NULL,std::vector<bool>(16,false)});
    rleRegions.push_back({adjacent+16,0,NULL,NULL,std::vector<bool>()});
    requireRle(rleBaseRegion(adjacent+16).count==0);rleRegions.clear();
    rleAllocationLimit=1<<20;
    for(int mode=0;mode<3;++mode)for(int oldW:{0,1,4,16})for(int oldH:{0,1,7,16})for(int w:{-4,-3,-2,-1,0,1,3,8,17,33})for(int h:{-4,-3,-2,-1,0,1,7,16})for(int fail:{-1,0,1}){
        History history(mode);history.allocate(oldW,oldH,-1);history.allocate(w,h,fail);history.allocate(w,h,-1);history.clear();history.allocate(3,2,-1);history.clear();history.clear();
    }
    for(int mode=0;mode<3;++mode){History history(mode);for(const auto& size:{CSize(4,16),CSize(8,8),CSize(1,64),CSize(64,1),CSize(-3,-4),CSize(0,32767)})history.allocate(size.cx,size.cy,-1);}
    requireRle(calls&&failures&&reuses&&negativeReuses&&excluded&&initialized&&unwritten&&rleOwners&&rleOwners==rleLoans&&rleLoans==rleReleases&&vobMemoryFailures==vobExceptionDeletes&&rleDeclines);
    printf("%llu exact VobSub Alloc calls passed (%llu retained byte/metadata fields / %llu initialized bytes / %llu unwritten cells tracked without reads / %llu allocation-cleanup observations / %llu modeled false returns / %llu capacity reuses including %llu negative-dimension reuse calls / %llu Rust owners, loans and releases / %llu modeled Rust declines; %llu negative allocation-count histories excluded before either call and NOT passes; pinned constructor/destructor/Alloc/Free, both capacity predicates, partial component writes, active-plane reset, zero-sized native allocation, first/second modeled CMemoryException cleanup, retained org after Free and errno/FP/Windows error state; allocation/exception boundaries controlled, uninitialized nOffset untouched, overflowing arithmetic, negative array allocations and actual OOM excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)initialized,(unsigned long long)unwritten,(unsigned long long)observations,(unsigned long long)failures,(unsigned long long)reuses,(unsigned long long)negativeReuses,(unsigned long long)rleOwners,(unsigned long long)rleDeclines,(unsigned long long)excluded);
}
