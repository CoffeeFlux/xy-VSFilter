// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_vobsub_trim.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
typedef std::array<RGBQUAD,1024> Storage;
struct Outcome {
    Storage first,second;std::array<RGBQUAD,256> selfFirst,selfSecond;std::vector<int64_t> states;
    bool operator==(const Outcome& other)const{
        return !std::memcmp(first.data(),other.first.data(),sizeof(Storage)) && !std::memcmp(second.data(),other.second.data(),sizeof(Storage))
            && !std::memcmp(selfFirst.data(),other.selfFirst.data(),sizeof(selfFirst)) && !std::memcmp(selfSecond.data(),other.selfSecond.data(),sizeof(selfSecond)) && states==other.states;
    }
};
static uint64_t comparisons=0,changed=0,aliases=0,selfCases=0,excluded=0;
static Storage initial(int width,int height,uint32_t mask,unsigned seed) {
    Storage result;
    for(size_t i=0;i<result.size();++i)result[i]=RGBQUAD{static_cast<uint8_t>(i*17+seed),static_cast<uint8_t>(i*31+seed),static_cast<uint8_t>(i*7+seed),0};
    for(int i=0;i<width*height;++i)result[96+i].rgbReserved=(mask>>(i%32))&1 ? static_cast<uint8_t>(1+(i+seed)%255) : 0;
    return result;
}
// Exclude only native memcpy calls whose individual ranges overlap. Other
// source/output overlap remains covered, including zeroing future source rows.
static bool admitted(const Storage& storage,int width,int height,int destination) {
    int l=width,t=height,r=0,b=0;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x)if(storage[96+y*width+x].rgbReserved){l=std::min(l,x);t=std::min(t,y);r=std::max(r,x);b=std::max(b,y);}
    if(l>r || t>b)return true;
    int w=r-l+1,h=b-t+1;
    if(width<=0 || height<=0)w=h=l=t=0;
    for(int y=0;y<h;++y) {
        const int src=96+(t+y)*width+l,dst=destination+(w+2)*(y+1)+1;
        if(w && src<dst+w && dst<src+w)return false;
    }
    return true;
}
template<class Image> static Outcome execute(const Storage& before,int width,int height,int left,int top,int destination,int repeats,bool self) {
    Outcome result;Storage storage=before;Image image;image.rect=CRect(left,top,left+width,top+height);
    image.lpTemp1=storage.data()+96;image.lpTemp2=self ? image.self : storage.data()+destination;image.lpPixels=storage.data()+3;
    for(unsigned i=0;i<256;++i)image.self[i]=before[i];
    for(int pass=0;pass<repeats;++pass) {
        image.TrimSubImage();
        result.states.insert(result.states.end(),{image.rect.left,image.rect.top,image.rect.right,image.rect.bottom,self && image.lpPixels==image.self ? int64_t(-1) : int64_t(image.lpPixels-storage.data())});
        if(pass==0){result.first=storage;std::memcpy(result.selfFirst.data(),image.self,sizeof(image.self));}
    }
    result.second=storage;std::memcpy(result.selfSecond.data(),image.self,sizeof(image.self));return result;
}
static void compare(int width,int height,uint32_t mask,unsigned seed,int destination=600,int repeats=2,bool self=false) {
    const auto before=initial(width,height,mask,seed);
    if(!self && !admitted(before,width,height,destination)){++excluded;return;}
    const int left=static_cast<int>(seed%13)-6,top=static_cast<int>(seed%17)-8;
    const auto a=execute<ReferenceVobTrim>(before,width,height,left,top,destination,repeats,self);
    const auto b=execute<CandidateVobTrim>(before,width,height,left,top,destination,repeats,self);
    if(!(a==b)){fprintf(stderr,"VobSub trim mismatch width=%d height=%d mask=%u seed=%u dst=%d repeats=%d self=%d\n",width,height,mask,seed,destination,repeats,self);std::abort();}
    ++comparisons;changed+=std::memcmp(a.first.data(),before.data(),sizeof(Storage))!=0;aliases+=destination!=600 && !self;selfCases+=self;
}
int main() {
    for(unsigned mask=0;mask<65536;++mask)compare(4,4,mask,mask);
    for(int width:{0,1,2,3,7,12})for(int height:{0,1,2,3,7,8})for(unsigned seed=0;seed<64;++seed)
    for(uint32_t mask:{0U,1U,0x80000000U,0xaaaaaaaaU,0x55555555U,0xffffffffU})compare(width,height,mask,seed);
    for(int width:{1,2,4,7,12})for(int height:{1,2,4,8})for(int delta:{-64,-32,-8,-4,0,4,8,32,64})
    for(uint32_t mask:{0U,1U,0x80000000U,0xaaaaaaaaU,0xffffffffU})compare(width,height,mask,17,96+delta,1);
    for(int width:{0,1,2,4,8})for(int height:{0,1,2,4,8})for(uint32_t mask:{0U,1U,0xaaaaaaaaU,0xffffffffU})compare(width,height,mask,31,600,1,true);
    for(int width:{-3,-1,0,1,3})for(int height:{-3,-1,0,1,3})if(width<0 || height<0)compare(width,height,0,0,600,1);
    // Copying a snapshot is observably different when the native border clears
    // upcoming input. Both admitted copies below have disjoint per-row memcpy.
    auto before=initial(4,4,0xffffffffU,17);
    const auto shared=execute<ReferenceVobTrim>(before,4,4,0,0,100,1,false);
    const auto separate=execute<ReferenceVobTrim>(before,4,4,0,0,600,1,false);
    if(!admitted(before,4,4,100) || !std::memcmp(shared.first.data()+100,separate.first.data()+600,36*sizeof(RGBQUAD)) || !cropsHandled || !cropsFallback || !changed){fprintf(stderr,"VobSub trim route/alias controls failed\n");return 1;}
    printf("%llu exact VobSub trim comparisons passed (%llu changed backing stores, %llu source/output alias cases, %llu protected-object cases, %llu Rust bounds scans, %llu native bounds fallbacks, %llu Rust crop copies, %llu native copy fallbacks, %llu overlapping-memcpy fixtures excluded; full source/output/guard snapshots, repeated trimming, empty dimensions and every 4x4 alpha occupancy mask)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(changed),static_cast<unsigned long long>(aliases),static_cast<unsigned long long>(selfCases),static_cast<unsigned long long>(boundsHandled),static_cast<unsigned long long>(boundsFallback),static_cast<unsigned long long>(cropsHandled),static_cast<unsigned long long>(cropsFallback),static_cast<unsigned long long>(excluded));
}
