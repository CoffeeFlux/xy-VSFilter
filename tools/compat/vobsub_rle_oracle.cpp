// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_vobsub_rle.h"
#include <array>
#include <cstdio>
#include <cstdlib>
typedef std::array<RGBQUAD,160> Storage;
struct Outcome {
    Storage pixels;
    std::array<RGBQUAD,256> trimmed;
    std::vector<uint64_t> events,state;
    bool operator==(const Outcome& other)const {
        return !std::memcmp(pixels.data(),other.pixels.data(),sizeof(pixels)) && !std::memcmp(trimmed.data(),other.trimmed.data(),sizeof(trimmed)) && events==other.events && state==other.state;
    }
};
static uint64_t comparisons=0,observations=0,interruptions=0,aliases=0,changed=0;
static Storage initial() {
    Storage result;
    for(size_t i=0;i<result.size();++i)result[i]=RGBQUAD{static_cast<BYTE>(i*17+3),static_cast<BYTE>(i*31+7),static_cast<BYTE>(i*13+11),static_cast<BYTE>(i*7+1)};
    return result;
}
template<class Image> static Outcome execute(const std::vector<BYTE>& bytes,size_t first,size_t second,size_t dataSize,int width,int height,bool custom,bool trim,bool allocate,int alias,int failure) {
    Outcome result;result.pixels=initial();Image image;
    for(size_t i=0;i<result.trimmed.size();++i)result.trimmed[i]=result.pixels[i%result.pixels.size()];
    image.rect=CRect(-3,7,width-3,height+7);image.nOffset[0]=first;image.nOffset[1]=second;
    image.nPlane=1;image.fAligned=false;image.fCustomPal=!custom;image.tridx=-77;image.allocate=allocate;
    image.backing=result.pixels.data();image.backingCount=result.pixels.size();image.lpTemp1=image.backing+32;image.lpPixels=image.backing+31;
    image.lpTemp2=result.trimmed.data()+8;
    image.orgpal=image.backing+4;image.cuspal=image.backing+24;
    for(unsigned i=0;i<4;++i){image.pal[i].pal=(i*3+1)&15;image.pal[i].tr=(i*5+3)&15;}
    auto input=bytes;BYTE* data=input.data();
    if(alias){data=reinterpret_cast<BYTE*>(image.lpTemp1)+alias;std::memcpy(data,bytes.data(),bytes.size());}
    rleEvents.clear();rleObservations=0;rleFailure=failure;
    bool returned=false,interrupted=false;
    try{returned=image.Decode(data,bytes.size(),dataSize,123,custom,9,image.backing+4,image.backing+24,trim);}
    catch(const RleInterrupted&){interrupted=true;}
    result.events=rleEvents;
    result.state={uint64_t(returned),uint64_t(interrupted),image.nOffset[0],image.nOffset[1],image.nPlane,uint64_t(image.fAligned),uint64_t(image.fCustomPal),uint64_t(static_cast<uint32_t>(image.tridx)),uint64_t(static_cast<uint32_t>(image.rect.left)),uint64_t(static_cast<uint32_t>(image.rect.top)),uint64_t(static_cast<uint32_t>(image.rect.right)),uint64_t(static_cast<uint32_t>(image.rect.bottom)),image.lpPixels==image.lpTemp2 ? uint64_t(-1) : uint64_t(image.lpPixels-image.backing),uint64_t(image.orgpal-image.backing),uint64_t(image.cuspal-image.backing)};
    return result;
}
static void compare(const std::vector<BYTE>& bytes,size_t first,size_t second,size_t size,int width,int height,bool custom=false,bool trim=false,bool allocate=true,int alias=0,int failure=-1) {
    const auto a=execute<ReferenceVobRle>(bytes,first,second,size,width,height,custom,trim,allocate,alias,failure);
    const auto b=execute<CandidateVobRle>(bytes,first,second,size,width,height,custom,trim,allocate,alias,failure);
    if(!(a==b)){fprintf(stderr,"DVD RLE mismatch first=%zu second=%zu size=%zu width=%d height=%d custom=%d trim=%d alloc=%d alias=%d failure=%d firstbyte=%u\n",first,second,size,width,height,custom,trim,allocate,alias,failure,bytes[0]);std::abort();}
    ++comparisons;observations+=a.events.size()/(10+a.pixels.size());interruptions+=a.state[1];aliases+=alias!=0;
    const auto before=initial();changed+=std::memcmp(a.pixels.data(),before.data(),sizeof(Storage))!=0;
}
static uint32_t rng=0x67452301;
static uint32_t nextRandom(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
int main() {
    for(unsigned word=0;word<65536;++word) {
        std::vector<BYTE> bytes(32,0);bytes[0]=static_cast<BYTE>(word>>8);bytes[1]=static_cast<BYTE>(word);
        bytes[2]=static_cast<BYTE>(~word>>8);bytes[3]=static_cast<BYTE>(~word);
        compare(bytes,0,2,4,1+(word%12),1+(word%8),word&1,word&2);
    }
    for(size_t first:{0,1,2,5,8})for(size_t second:{0,1,2,5,8})for(size_t size:{0,1,2,5,8,16})
    for(int width:{1,2,7,12})for(int height:{1,3,8})for(bool custom:{false,true})for(bool trim:{false,true}) {
        std::vector<BYTE> bytes(48);for(auto& byte:bytes)byte=static_cast<BYTE>(nextRandom());
        compare(bytes,first,second,size,width,height,custom,trim);
    }
    for(unsigned i=0;i<12000;++i) {
        std::vector<BYTE> bytes(96);for(auto& byte:bytes)byte=static_cast<BYTE>(nextRandom());
        const size_t first=nextRandom()%9,second=nextRandom()%33,size=nextRandom()%65;
        compare(bytes,first,second,size,1+(i%12),1+(i%8),i&1,i&2,true,i%3 ? 4 : 0);
    }
    // Boundary exceptions are thrown only by native wrappers, never across Rust.
    for(unsigned byte:{0U,1U,4U,15U,64U,255U})for(int failure=-1;failure<35;++failure)for(bool allocate:{false,true}) {
        std::vector<BYTE> bytes(32,static_cast<BYTE>(byte));
        compare(bytes,0,4,8,7,3,byte&1,true,allocate,0,failure);
    }
    // Explicit route controls: both raw bytes and output must be observed.
    std::vector<BYTE> bytes(32,0x55);
    const auto control=execute<ReferenceVobRle>(bytes,0,4,8,7,3,false,true,true,0,-1);
    const auto shared=execute<ReferenceVobRle>(bytes,0,4,8,7,3,false,true,true,4,-1);
    std::vector<uint64_t> separateNibbles,sharedNibbles;
    for(size_t i=0;i<control.events.size();i+=170)if(control.events[i]==4)separateNibbles.push_back(control.events[i+4]);
    for(size_t i=0;i<shared.events.size();i+=170)if(shared.events[i]==4)sharedNibbles.push_back(shared.events[i+4]);
    const auto before=initial();
    if(separateNibbles==sharedNibbles || !nibbleCalls || !codeCalls || !cropsHandled || !interruptions || !aliases || !std::memcmp(control.pixels.data(),before.data(),sizeof(Storage))){fprintf(stderr,"DVD RLE route/visibility control failed\n");return 1;}
    printf("%llu exact DVD RLE comparisons passed (%llu full state/pixel boundary observations, %llu injected native interruptions, %llu input/output alias cases, %llu changed backing stores, %llu Rust nibble calls, %llu Rust code steps; every four-nibble prefix, alternating planes, byte alignment, short-circuit consumption, native draw order and actual image trimming)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(observations),static_cast<unsigned long long>(interruptions),static_cast<unsigned long long>(aliases),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(nibbleCalls),static_cast<unsigned long long>(codeCalls));
}
