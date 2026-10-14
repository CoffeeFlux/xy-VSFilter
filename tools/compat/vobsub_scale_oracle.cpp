// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_vobsub_scale.h"
#include <array>
#include <cstdio>
typedef std::array<DWORD,256> Storage;
struct Outcome {
    Storage first,second;std::vector<size_t> allocations;bool interrupted;CRect rect;ptrdiff_t pixels;
    bool operator==(const Outcome& other)const{return first==other.first && second==other.second && allocations==other.allocations && interrupted==other.interrupted && rect.left==other.rect.left && rect.top==other.rect.top && rect.right==other.rect.right && rect.bottom==other.rect.bottom && pixels==other.pixels;}
};
static uint64_t comparisons=0,changed=0,interruptions=0,allocations=0;
template<class Image> static Outcome execute(Storage storage,int width,int height,int failure) {
    Image image;image.rect=CRect(-7,13,width-7,height+13);image.lpPixels=storage.data()+16;
    scaleAllocations.clear();scaleFailure=failure;Outcome result;result.interrupted=false;
    try{image.Scale2x();}catch(const ScaleInterrupted&){result.interrupted=true;}
    result.first=storage;
    if(!result.interrupted){try{image.Scale2x();}catch(const ScaleInterrupted&){result.interrupted=true;}}
    result.second=storage;result.allocations=scaleAllocations;result.rect=image.rect;result.pixels=image.lpPixels-storage.data();return result;
}
static void compare(const Storage& storage,int width,int height,int failure=-1) {
    const auto a=execute<ReferenceVobScale>(storage,width,height,failure);
    const auto b=execute<CandidateVobScale>(storage,width,height,failure);
    if(!(a==b)){fprintf(stderr,"VobSub Scale2x mismatch width=%d height=%d failure=%d\n",width,height,failure);std::abort();}
    ++comparisons;changed+=a.first!=storage;interruptions+=a.interrupted;allocations+=a.allocations.size();
}
static uint32_t rng=0x10293847;
static uint32_t nextRandom(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static Storage initial() {Storage result;for(auto& value:result)value=nextRandom();return result;}
int main() {
    const DWORD colors[]={0,0xffffffffU,0x01020304U,0x7f80feffU,0x11223344U};
    for(unsigned combination=0;combination<3125;++combination) {
        auto storage=initial();unsigned code=combination;
        for(unsigned offset:{1U,3U,4U,5U,7U}){storage[16+offset]=colors[code%5];code/=5;}
        compare(storage,3,3);
    }
    for(unsigned a=0;a<256;++a)for(unsigned b=0;b<256;++b) {
        auto storage=initial();
        const DWORD first=a|(b<<8)|((255-a)<<16)|((255-b)<<24),second=b|(a<<8)|((255-b)<<16)|((255-a)<<24);
        storage[17]=storage[19]=first;storage[21]=storage[23]=second;storage[20]=0x807fff01;
        compare(storage,3,3);
    }
    for(int width:{-2,0,1,2,3,7,11})for(int height:{-2,0,1,2,3,7,9})for(unsigned seed=0;seed<128;++seed) {
        auto storage=initial();
        if(seed&1)for(auto& value:storage)value=colors[value%5];
        compare(storage,width,height);
    }
    for(int width:{1,3,11})for(int height:{1,3,9})for(int failure:{0,1})compare(initial(),width,height,failure);
    auto control=initial();control[17]=control[19]=control[20]=0;control[21]=control[23]=0xffffffffU;
    const auto native=execute<ReferenceVobScale>(control,3,3,-1);
    if(native.first[20]!=0x3f3f4040U || !changed || !scaleHandled || interruptions!=18){fprintf(stderr,"VobSub Scale2x rounding/route controls failed\n");return 1;}
    // Allocation exceptions occur before any pixel access.
    ReferenceVobScale ref;CandidateVobScale candidate;
    ref.rect=candidate.rect=CRect(0,0,1,1);ref.lpPixels=candidate.lpPixels=nullptr;
    bool a=false,b=false;scaleAllocations.clear();scaleFailure=0;try{ref.Scale2x();}catch(const ScaleInterrupted&){a=true;}
    scaleAllocations.clear();scaleFailure=0;try{candidate.Scale2x();}catch(const ScaleInterrupted&){b=true;}
    if(!a || !b){fprintf(stderr,"VobSub Scale2x allocation-order control failed\n");return 1;}
    printf("%llu exact VobSub Scale2x comparisons passed (%llu bytes across two full image/guard checkpoints, %llu visibly changed cases, %llu observed allocations, %llu injected allocation interruptions, %llu Rust-filled images, %llu native fallback images; all five-neighbor equality assignments, channel pairs, edge substitution and unchanged image dimensions)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(comparisons*2*sizeof(Storage)),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(allocations),static_cast<unsigned long long>(interruptions),static_cast<unsigned long long>(scaleHandled),static_cast<unsigned long long>(scaleFallback));
}
