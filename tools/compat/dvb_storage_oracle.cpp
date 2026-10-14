// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_dvb_storage.h"
static uint64_t calls=0,fields=0,observations=0,initialized=0,unwritten=0,rejections=0,ignored=0,growths=0,reuses=0,aliases=0,limitBypasses=0;
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
    std::vector<uint64_t> out={static_cast<uint64_t>(object.m_nBufferSize),static_cast<uint64_t>(object.m_nBufferReadPos),static_cast<uint64_t>(object.m_nBufferWritePos),object.m_pBuffer?1u:0u,static_cast<uint64_t>(object.m_pCurrentPage.value?object.m_pCurrentPage.value->id:0),object.m_Pages.GetCount()};
    for(const auto* page:object.m_Pages.values)out.push_back(page->id);
    if(object.m_pBuffer){size_t offset;const auto& region=rleRegion(object.m_pBuffer,object.m_nBufferSize,offset);requireRle(!offset);rleGuards(region);
        for(size_t i=0;i<region.count;++i){out.push_back(region.initialized[i]);if(region.initialized[i]){out.push_back(region.data[i]);initialized+=count;}else unwritten+=count;}
    }
    return out;
}
struct History {
    ReferenceDvbStorage::CDVBSub* a;CandidateDvbStorage::CDVBSub* b;
    int mode;unsigned operation=0;
    explicit History(int allocationMode):mode(allocationMode){
        requireRle(rleRegions.empty());a=new ReferenceDvbStorage::CDVBSub();b=new CandidateDvbStorage::CDVBSub();
        a->m_pCurrentPage.value=new StoragePage(100);b->m_pCurrentPage.value=new StoragePage(100);
        for(int i=1;i<=3;++i){a->m_Pages.values.push_back(new StoragePage(i));b->m_Pages.values.push_back(new StoragePage(i));}
        requireRle(snapshot(*a,false)==snapshot(*b,false));
    }
    ~History(){rleEvents.clear();delete a;const auto trace=rleEvents;rleEvents.clear();delete b;requireRle(trace==rleEvents&&rleRegions.empty());observations+=trace.size();}
    HRESULT append(int size,bool first,unsigned fourth=19,int prefixIndex=-1,BYTE prefixValue=0,bool alias=false){
        requireRle(size>=0&&size<=90000);
        // Native start-marker recognition loads a LONG even for short logical
        // chunks. Give it aligned, initialized four-byte backing in every case.
        std::vector<LONG> storage((static_cast<size_t>(size)+7)/4,0);BYTE* data=reinterpret_cast<BYTE*>(storage.data());const size_t backing=storage.size()*sizeof(LONG);
        for(size_t i=0;i<backing;++i)data[i]=static_cast<BYTE>(i*37+fourth);
        data[0]=first?0x20:0x21;data[1]=0;data[2]=0x0f;data[3]=static_cast<BYTE>(fourth);
        if(prefixIndex>=0)data[prefixIndex]=prefixValue;const auto source=storage;
        rleRegions.push_back({data,backing,NULL,NULL,std::vector<bool>(backing,true)});
        if(alias){requireRle(!first&&size<=a->m_nBufferWritePos-4&&size+a->m_nBufferWritePos<=a->m_nBufferSize);++aliases;}
        rleDecline=mode==1||(mode==2&&operation%2);++operation;const int oldCapacity=a->m_nBufferSize;
        BYTE* oldA=a->m_pBuffer;BYTE* oldB=b->m_pBuffer;
        rleEvents.clear();seed();HRESULT ra=a->AddToBuffer(alias?a->m_pBuffer+4:data,size);Effects ea;const auto trace=rleEvents;const auto sa=snapshot(*a,true);
        rleEvents.clear();seed();HRESULT rb=b->AddToBuffer(alias?b->m_pBuffer+4:data,size);Effects eb;
        if(ra!=rb||!(ea==eb)||trace!=rleEvents||sa!=snapshot(*b,false)||source!=storage){fprintf(stderr,"DVB buffer mismatch call=%llu size=%d first=%d mode=%d\n",(unsigned long long)calls,size,first,mode);std::abort();}
        ++calls;fields+=sa.size();observations+=trace.size();rejections+=ra==E_INVALIDARG;ignored+=ra==S_FALSE;
        if(oldCapacity==a->m_nBufferSize){requireRle(oldA==a->m_pBuffer&&oldB==b->m_pBuffer);++reuses;}else ++growths;
        bool removed=false;for(size_t i=0;i<rleRegions.size();++i)if(rleRegions[i].data==data){rleRegions.erase(rleRegions.begin()+i);removed=true;break;}requireRle(removed);return ra;
    }
    void advance(){a->m_nBufferReadPos=b->m_nBufferReadPos=a->m_nBufferWritePos/2;}
    void reset(){rleEvents.clear();a->Reset();const auto trace=rleEvents;rleEvents.clear();b->Reset();requireRle(trace==rleEvents&&snapshot(*a,false)==snapshot(*b,false));}
};
int main(){
    rleAllocationLimit=131072;
    for(int mode=0;mode<3;++mode)for(int first:{1,2,3,4,15,4095,4096,4097})for(unsigned salt:{0u,1u,255u}){
        History h(mode);h.append(0,false,salt);h.append(4,false,salt);h.append(first,true,salt);h.advance();
        for(int chunk:{0,1,3,17,4096,8192}){h.append(chunk,false,salt);h.advance();}
        h.append(3,true,salt);h.append(0,true,salt);h.append(1,false,salt);h.append(48,true,salt);h.append(16,false,salt,-1,0,true);h.reset();h.append(4,false,salt);h.append(4,true,salt);
    }
    for(unsigned fourth=0;fourth<256;++fourth){History h(0);h.append(4,true,fourth);h.append(1,false,fourth);}
    for(int index=0;index<3;++index)for(unsigned value=0;value<256;++value){History h(0);h.append(4,true,17,index,static_cast<BYTE>(value));}
    for(int mode=0;mode<3;++mode){
        {History h(mode);requireRle(h.append(81921,true)==E_INVALIDARG);requireRle(h.append(81920,true)==S_OK);requireRle(h.append(1,false)==E_INVALIDARG);h.append(1,true);}
        {History h(mode);requireRle(h.append(80000,true)==S_OK);requireRle(h.append(1024,false)==S_OK);requireRle(h.a->m_nBufferSize==84096);requireRle(h.append(5000,false)==E_INVALIDARG);requireRle(h.append(83000,true)==S_OK);++limitBypasses;h.reset();h.append(1,true);}
    }
    requireRle(calls&&rejections&&ignored&&growths&&reuses&&aliases&&limitBypasses==3&&unwritten&&rleOwners&&rleOwners==rleLoans&&rleLoans==rleReleases&&rleDeclines);
    printf("%llu exact whole-DVB AddToBuffer calls passed (%llu retained state fields / %llu initialized bytes / %llu unwritten cells tracked without reads / %llu allocation-copy-reset observations / %llu rejections / %llu ignored chunks / %llu growths / %llu capacity reuses / %llu disjoint source aliases / %llu retained-capacity size-limit bypasses / %llu Rust owners, loans and releases / %llu modeled Rust declines; pinned constructor/destructor/Reset and buffer assembly, every marker byte and ignored fourth byte, short logical inputs with four-byte aligned backing, consumed-prefix retention, split/restart/reuse, growth-before-copy-before-release ordering and errno/FP/Windows error state; allocations/deletions, successful memcpy_s and page containers controlled; negative/overflowing sizes, short physical backing, overlapping copies, aliases invalidated by growth, native debug traps and actual OOM excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)initialized,(unsigned long long)unwritten,(unsigned long long)observations,(unsigned long long)rejections,(unsigned long long)ignored,(unsigned long long)growths,(unsigned long long)reuses,(unsigned long long)aliases,(unsigned long long)limitBypasses,(unsigned long long)rleOwners,(unsigned long long)rleDeclines);
}
