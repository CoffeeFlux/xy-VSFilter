// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_vobsub_path.h"
#include <climits>
struct Outcome {
    std::wstring output;std::vector<std::wstring> events;int attempts;bool returned,interrupted,mutated;
    bool operator==(const Outcome& o)const{return output==o.output && events==o.events && attempts==o.attempts && returned==o.returned && interrupted==o.interrupted && mutated==o.mutated;}
};
static uint64_t comparisons=0,interruptions=0,mutations=0,operations=0,rejections=0;
template<class Image> static Outcome execute(const std::vector<BYTE>& types,int failure,bool mutate,bool accepted,unsigned seed){
    Image image;image.rect.left=int(seed%131)-65;image.rect.top=int(seed%97)-48;
    PathString output;output.value=L"prior ";output.value.push_back(0);output.value+=L"text";
    pathOutput=&output;pathInputTypes=types;pathInputPoints.clear();
    for(size_t i=0;i<types.size();++i)pathInputPoints.push_back(CPoint(long(i)*17-70,50-long(i)*29));
    if(seed%31==0){
        uint32_t random=seed^0x98753412;
        for(auto& p:pathInputPoints){
            random^=random<<13;random^=random>>17;random^=random<<5;p.x=long(int64_t(random%0xfffffff0u)-0x7ffffff8LL);
            random^=random<<13;random^=random>>17;random^=random<<5;p.y=long(int64_t(random%0xfffffff0u)-0x7ffffff8LL);
        }
    }
    if(seed==UINT_MAX){for(size_t i=0;i<types.size();++i)pathInputPoints[i]=CPoint(i%2?INT_MIN:INT_MAX,i%2?INT_MAX:INT_MIN);}
    pathEvents.clear();pathFailure=failure;pathAttempts=0;pathMutate=mutate;pathMutated=false;pathPolygonResult=accepted;
    Outcome result;result.returned=false;result.interrupted=false;
    try{result.returned=image.Polygonize(output,seed%2!=0,int(seed%17)-8);}catch(const PathInterrupted&){result.interrupted=true;}
    result.output=output.value;result.events=pathEvents;result.attempts=pathAttempts;result.mutated=pathMutated;return result;
}
static void compare(const std::vector<BYTE>& types,int failure=-1,bool mutate=false,bool accepted=true,unsigned seed=0){
    auto a=execute<ReferenceVobPath>(types,failure,mutate,accepted,seed);auto b=execute<CandidateVobPath>(types,failure,mutate,accepted,seed);
    if(!(a==b)){fprintf(stderr,"Path mismatch count=%zu failure=%d mutate=%d accepted=%d seed=%u\n",types.size(),failure,mutate,accepted,seed);std::abort();}
    ++comparisons;interruptions+=a.interrupted;mutations+=a.mutated;operations+=a.attempts;rejections+=!accepted;
}
int main(){
    const BYTE kinds[]={0,PT_MOVETO,PT_MOVETONC,PT_LINETO,PT_BEZIERTO,PT_BSPLINETO,PT_BSPLINEPATCHTO,7};
    unsigned combinations=1;
    for(int count=0;count<=5;++count){
        if(count)combinations*=8;
        for(unsigned value=0;value<combinations;++value){unsigned code=value;std::vector<BYTE> types;
            for(int i=0;i<count;++i){types.push_back(kinds[code%8]);code/=8;}
            compare(types,-1,false,true,value);if(value%7==0)compare(types,-1,true,true,value);
        }
    }
    for(unsigned type=0;type<256;++type)for(int count=1;count<=7;++count)compare(std::vector<BYTE>(count,BYTE(type)),-1,false,true,UINT_MAX);
    uint32_t random=0x31587112;
    for(unsigned value=0;value<10000;++value){
        random^=random<<13;random^=random>>17;random^=random<<5;unsigned count=random%33;std::vector<BYTE> types;
        for(unsigned i=0;i<count;++i){random^=random<<13;random^=random>>17;random^=random<<5;types.push_back(value%2?kinds[random%8]:BYTE(random));}
        compare(types,-1,value%3==0,value%19!=0,value);
    }
    const std::vector<BYTE> mixed={6,2,4,2,6,4,2,6,0xfc,0,2,0xfa,0xfe,4};
    for(int failure=0;failure<48;++failure){compare(mixed,failure,false);compare(mixed,failure,true);}
    const auto changed=execute<ReferenceVobPath>(mixed,-1,true,true,0);
    const auto unchanged=execute<ReferenceVobPath>(mixed,-1,false,true,0);
    const auto control=execute<ReferenceVobPath>({4,2,6,4,2,6},-1,false,true,0);
    if(changed.output==unchanged.output || control.output.find(L"b ",control.output.find(L"b ")+1)==std::wstring::npos || !mutations || !interruptions || !pathCommands){fprintf(stderr,"Path grouping/callback controls failed\n");return 1;}
    printf("%llu exact VobSub ASS-path comparisons passed (%llu native format/append operations, %llu injected interruptions, %llu callback-mutated paths, %llu rejected polygon conversions, %llu Rust commands; all byte path types, truncated and mixed curve triples, previous type from final consumed entry, integer limits and live point reads after prefix append)\n",
        (unsigned long long)comparisons,(unsigned long long)operations,(unsigned long long)interruptions,(unsigned long long)mutations,(unsigned long long)rejections,(unsigned long long)pathCommands);
}
