// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_vobsub_split.h"
#include <climits>
struct Outcome {
    std::vector<int64_t> source,first,second,events;int attempts,assertions;bool interrupted,mutated;
    bool operator==(const Outcome& o)const{return source==o.source && first==o.first && second==o.second && events==o.events && attempts==o.attempts && assertions==o.assertions && interrupted==o.interrupted && mutated==o.mutated;}
};
static uint64_t comparisons=0,assertions=0,interruptions=0,appends=0,mutations=0,aliases=0,direct=0;
template<class Image> static Outcome execute(const std::vector<int>& directions,int failure,bool mutate,bool alias,unsigned seed){
    Image image;COutline input(0),first(1),second(2);input.da.values=directions;
    for(size_t i=0;i<directions.size();++i)input.pa.values.push_back(CPoint(int(i)*17-70+int(seed%31),50-int(i)*29-int(seed%43)));
    first.pa.values={CPoint(-3,7),CPoint(8,-9)};first.da.values={17};
    second.pa.values={CPoint(2,3)};second.da.values={-18,19};
    splitSource=&input;splitFirst=&first;splitSecond=alias?&first:&second;
    splitEvents.clear();splitFailure=failure;splitAttempts=splitAssertions=splitTableId=0;splitMutate=mutate;splitMutated=false;
    Outcome result;result.interrupted=false;
    try{image.SplitOutline(input,first,*splitSecond);}catch(const SplitInterrupted&){result.interrupted=true;}
    result.source=splitState(input);result.first=splitState(first);result.second=splitState(second);result.events=splitEvents;
    result.attempts=splitAttempts;result.assertions=splitAssertions;result.mutated=splitMutated;return result;
}
static void compare(const std::vector<int>& directions,int failure=-1,bool mutate=false,bool alias=false,unsigned seed=0){
    auto a=execute<ReferenceVobSplit>(directions,failure,mutate,alias,seed);
    auto b=execute<CandidateVobSplit>(directions,failure,mutate,alias,seed);
    if(!(a==b)){fprintf(stderr,"Split mismatch count=%zu failure=%d mutate=%d alias=%d seed=%u\n",directions.size(),failure,mutate,alias,seed);std::abort();}
    ++comparisons;assertions+=a.assertions;interruptions+=a.interrupted;appends+=a.attempts;mutations+=a.mutated;aliases+=alias;
}
int main(){
    unsigned combinations=1;
    for(int count=0;count<=9;++count){
        if(count)combinations*=3;
        for(unsigned value=0;value<combinations;++value){unsigned code=value;std::vector<int> dirs;
            for(int i=0;i<count;++i){dirs.push_back(int(code%3)-1);code/=3;}
            compare(dirs,-1,false,false,value);compare(dirs,-1,value%3==0,true,value);
        }
    }
    uint32_t random=0x9d117573;const int values[]={INT_MIN,-3,-1,0,1,2,INT_MAX};
    for(unsigned i=0;i<12000;++i){
        random^=random<<13;random^=random>>17;random^=random<<5;int count=4+int(random%29);std::vector<int> dirs;
        for(int j=0;j<count;++j){random^=random<<13;random^=random>>17;random^=random<<5;dirs.push_back(values[random%7]);}
        compare(dirs,-1,i%4==0,i%5==0,i);
    }
    for(int count:{4,5,8,12})for(int failure=0;failure<85;++failure)for(bool alias:{false,true}){
        compare(std::vector<int>(count,1),failure,false,alias);compare(std::vector<int>(count,0),failure,true,alias);
    }
    // The table-only oracle checks UINT-width wrap without indexing huge outlines.
    COutline dummy;splitSource=splitFirst=splitSecond=&dummy;splitFailure=-1;splitMutate=false;
    const UINT words[]={0,1,2,3,0x7fffffff,0x80000000,0xfffffffe,0xffffffff};
    for(UINT left:words)for(UINT right:words)for(unsigned code=0;code<256;++code){
        CAtlArray<UINT> la,sa,ea;unsigned value=code;
        for(int i=0;i<4;++i){la.values.push_back(value%4);value/=4;sa.values.push_back(left);ea.values.push_back(right);}
        // All-zero tables have no native maximum; do not execute that unresolved path.
        if(code==0)continue;
        splitEvents.clear();splitAssertions=0;auto a=ReferenceVobSplitSelect(la,sa,ea);auto events=splitEvents;int diagnostics=splitAssertions;
        splitEvents.clear();splitAssertions=0;auto b=CandidateVobSplitSelect(la,sa,ea);
        if(a!=b || events!=splitEvents || diagnostics!=splitAssertions){fprintf(stderr,"Split table mismatch\n");return 1;}++direct;
    }
    auto changed=execute<ReferenceVobSplit>({0,0,0,0},-1,true,false,0);
    auto unchanged=execute<ReferenceVobSplit>({0,0,0,0},-1,false,false,0);
    if(changed.first==unchanged.first || changed.second==unchanged.second || !mutations || !interruptions || !assertions || !splitHandled || !splitFallback){fprintf(stderr,"Split route/append controls failed\n");return 1;}
    printf("%llu exact VobSub split comparisons and %llu direct table selections passed (%llu matching missing-interior assertions, %llu injected append interruptions, %llu observed temporary/output append attempts, %llu source mutations during output append, %llu aliased-output cases, %llu Rust selections, %llu native fallback selections; first maximum/interior ties, UINT wrapping, unchanged short inputs, midpoint timing and partial paired arrays)\n",
        (unsigned long long)comparisons,(unsigned long long)direct,(unsigned long long)assertions,(unsigned long long)interruptions,(unsigned long long)appends,(unsigned long long)mutations,(unsigned long long)aliases,(unsigned long long)splitHandled,(unsigned long long)splitFallback);
}
