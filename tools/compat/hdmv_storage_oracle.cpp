// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_hdmv_storage.h"
static uint64_t calls=0,fields=0,observations=0,initialized=0,unwritten=0,rejected=0,growths=0,reuses=0;
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
    std::vector<uint64_t> out={static_cast<uint64_t>(object.m_nCurSegment),static_cast<uint64_t>(object.m_nTotalSegBuffer),static_cast<uint64_t>(object.m_nSegBufferPos),static_cast<uint64_t>(object.m_nSegSize),object.m_pSegBuffer?1u:0u};
    if(object.m_pSegBuffer){size_t offset;const auto& region=rleRegion(object.m_pSegBuffer,object.m_nTotalSegBuffer,offset);requireRle(!offset);rleGuards(region);
        for(size_t i=0;i<region.count;++i){out.push_back(region.initialized[i]);if(region.initialized[i]){out.push_back(region.data[i]);initialized+=count;}else unwritten+=count;}
    }
    return out;
}
struct History {
    ReferenceSegment::CHdmvSub* a;CandidateSegment::CHdmvSub* b;
    explicit History(bool decline){requireRle(rleRegions.empty());rleDecline=decline;a=new ReferenceSegment::CHdmvSub();b=new CandidateSegment::CHdmvSub();requireRle(snapshot(*a,false)==snapshot(*b,false));}
    ~History(){
        // Destructor keeps Reset-before-buffer-before-current-presentation order.
        a->m_pCurrentPresentationSegment=new DummyPresentation();b->m_pCurrentPresentationSegment=new DummyPresentation();
        rleEvents.clear();delete a;const auto trace=rleEvents;rleEvents.clear();delete b;requireRle(trace==rleEvents&&rleRegions.empty());observations+=trace.size();
    }
    void sample(const std::vector<BYTE>& bytes,REFERENCE_TIME time=13,bool null=false){
        std::vector<BYTE> input=bytes;input.push_back(0x91);const auto original=input;
        rleRegions.push_back({input.data(),bytes.size(),NULL,NULL,std::vector<bool>(bytes.size(),true)});
        const int oldCapacity=a->m_nTotalSegBuffer;BYTE* oldA=a->m_pSegBuffer;BYTE* oldB=b->m_pSegBuffer;
        rleEvents.clear();seed();HRESULT ra=a->ParseSample(null?NULL:input.data(),static_cast<int>(bytes.size()),time,time+37);Effects ea;const auto trace=rleEvents;const auto sa=snapshot(*a,true);
        rleEvents.clear();seed();HRESULT rb=b->ParseSample(null?NULL:input.data(),static_cast<int>(bytes.size()),time,time+37);Effects eb;
        requireRle(ra==rb&&ea==eb&&trace==rleEvents&&sa==snapshot(*b,false)&&input==original);
        ++calls;fields+=sa.size();observations+=trace.size();rejected+=ra!=S_OK;
        if(a->m_nTotalSegBuffer==oldCapacity){requireRle(oldA==a->m_pSegBuffer&&oldB==b->m_pSegBuffer);++reuses;}else ++growths;
        bool removed=false;for(size_t i=0;i<rleRegions.size();++i)if(rleRegions[i].data==input.data()){rleRegions.erase(rleRegions.begin()+i);removed=true;break;}requireRle(removed);
    }
    void reset(){
        const auto state=snapshot(*a,false);rleEvents.clear();a->Reset();const auto trace=rleEvents;rleEvents.clear();b->Reset();requireRle(trace==rleEvents&&state==snapshot(*a,false)&&state==snapshot(*b,false));
    }
};
static std::vector<BYTE> packet(unsigned type,int declared,int supplied,unsigned salt){
    std::vector<BYTE> out={static_cast<BYTE>(type),static_cast<BYTE>(declared>>8),static_cast<BYTE>(declared)};
    for(int i=0;i<supplied;++i)out.push_back(static_cast<BYTE>(i*37+salt));return out;
}
int main(){
    rleAllocationLimit=65535;
    for(unsigned type=0;type<256;++type)for(int size:{0,1,2,3,7,16,33})for(bool decline:{false,true}){
        History history(decline);history.sample({});history.sample(packet(type,size,size,type));history.reset();history.sample({},-127,true);
        history.sample(packet(0x14,17,17,type));history.sample(packet(0x15,3,3,type+1));history.sample(packet(0x16,32,32,type+2));history.sample(packet(0x80,0,0,0));
    }
    for(unsigned type:{0x14,0x15,0x16,0x80})for(int size:{1,3,17,255,1024})for(int first:{0,1,3,7})if(first<=size)for(bool decline:{false,true}){
        History history(decline);history.sample(packet(type,size,first,91));history.reset();
        const auto whole=packet(type,size,size,91);size_t position=3+first;
        for(int chunk:{0,1,7,31,127,1024}){size_t end=std::min(whole.size(),position+chunk);history.sample(std::vector<BYTE>(whole.begin()+position,whole.begin()+end),31+position);position=end;}
        history.sample(std::vector<BYTE>(whole.begin()+position,whole.end()),99);
        history.sample(packet(0x14,1,1,19));
    }
    // Multiple segments preserve the caller's remaining-length bookkeeping.
    // A final short copy may still dispatch a declared-complete segment; only
    // initialized bytes and the uninitialized mask are observed by callbacks.
    for(unsigned type:{0x14,0x15,0x16})for(int first:{0,1,7,17})for(int tail:{0,1,2,3,7}){
        History history(false);auto input=packet(0x14,first,first,19);auto next=packet(type,17,tail,37);input.insert(input.end(),next.begin(),next.end());history.sample(input);history.sample(packet(0x80,0,0,0));
    }
    // Short headers and every ignored/rejected segment tag retain reader state.
    for(unsigned type=0;type<256;++type){History history(false);history.sample({static_cast<BYTE>(type)});history.sample({static_cast<BYTE>(type),0xff});}
    for(int size:{32767,32768,65534,65535})for(bool decline:{false,true}){
        History history(decline);history.sample(packet(0x15,size,size,91));
        history.sample(packet(0x16,1,1,17));history.sample(packet(0x80,0,0,0));
    }
    requireRle(calls&&rejected&&growths&&reuses&&unwritten&&partialDispatches&&rleOwners&&rleOwners==rleLoans&&rleLoans==rleReleases&&rleDeclines);
    printf("%llu exact whole-HDMV ParseSample calls passed (%llu retained state fields / %llu initialized bytes / %llu unwritten cells tracked without reads / %llu allocation-reader-dispatch observations / %llu rejections / %llu growths / %llu capacity reuses / %llu Rust owners, loans and releases / %llu modeled Rust declines; %llu controlled dispatches including %llu partial-initialization observations; complete pinned constructor, destructor, AllocSegment and byte-sample parser with real reference/candidate Golomb readers, all tags, split/short/multiple packets, retained tails, null/empty samples and errno/FP/Windows error state; presentation/segment consumers and Reset are controlled, partial tails never read, negative caller lengths, source-buffer aliasing, native debug traps and actual OOM excluded)\n",(unsigned long long)calls,(unsigned long long)fields,(unsigned long long)initialized,(unsigned long long)unwritten,(unsigned long long)observations,(unsigned long long)rejected,(unsigned long long)growths,(unsigned long long)reuses,(unsigned long long)rleOwners,(unsigned long long)rleDeclines,(unsigned long long)segmentDispatches,(unsigned long long)partialDispatches);
}
