// SPDX-License-Identifier: GPL-3.0-or-later
#include "original_vobsub_segment.h"
#include <climits>
struct Outcome {
    std::vector<int64_t> source,output,events;int returned,attempts,assertions;bool interrupted,mutated;
    bool operator==(const Outcome& other)const{return source==other.source && output==other.output && events==other.events && returned==other.returned && attempts==other.attempts && assertions==other.assertions && interrupted==other.interrupted && mutated==other.mutated;}
};
static uint64_t comparisons=0,assertions=0,interruptions=0,appends=0,mutations=0;
template<class Image> static Outcome execute(const std::vector<int>& directions,int count,int start,int failure,bool mutate,unsigned seed) {
    Image image;COutline input,output;
    input.da.values=directions;
    for(int i=0;i<count;++i)input.pa.values.push_back(CPoint((i*17+int(seed%31))-70,50-(i*29+int(seed%43))));
    output.pa.values={CPoint(-3,7),CPoint(8,-9),CPoint(19,2)};output.da.values={17,-18};
    segmentSource=&input;segmentOutput=&output;segmentEvents.clear();segmentFailure=failure;segmentAttempts=segmentAssertions=0;segmentMutate=mutate;segmentMutated=false;
    Outcome result;result.interrupted=false;result.returned=INT_MIN;
    try{result.returned=image.GrabSegment(start,input,output);}catch(const SegmentInterrupted&){result.interrupted=true;}
    result.source=segmentState(input);result.output=segmentState(output);result.events=segmentEvents;
    result.attempts=segmentAttempts;result.assertions=segmentAssertions;result.mutated=segmentMutated;return result;
}
static void compare(const std::vector<int>& directions,int count,int start,int failure=-1,bool mutate=false,unsigned seed=0) {
    const auto a=execute<ReferenceVobSegment>(directions,count,start,failure,mutate,seed);
    const auto b=execute<CandidateVobSegment>(directions,count,start,failure,mutate,seed);
    if(!(a==b)){fprintf(stderr,"VobSub segment mismatch count=%d dirs=%zu start=%d failure=%d mutate=%d seed=%u\n",count,directions.size(),start,failure,mutate,seed);std::abort();}
    ++comparisons;assertions+=a.assertions;interruptions+=a.interrupted;appends+=a.attempts;mutations+=a.mutated;
}
int main() {
    unsigned combinations=1;
    for(int count=1;count<=8;++count) {
        combinations*=3;
        for(unsigned value=0;value<combinations;++value) {
            unsigned code=value;std::vector<int> directions;
            for(int i=0;i<count;++i){directions.push_back(int(code%3)-1);code/=3;}
            for(int start=0;start<count;++start)compare(directions,count,start,-1,false,value);
        }
    }
    uint32_t random=0x14327865;
    const int values[]={INT_MIN,-3,-1,0,1,2,INT_MAX};
    for(unsigned i=0;i<12000;++i) {
        random^=random<<13;random^=random>>17;random^=random<<5;
        const int count=1+int(random%32),start=int((random>>8)%count);std::vector<int> directions;
        for(int j=0;j<count;++j){random^=random<<13;random^=random>>17;random^=random<<5;directions.push_back(values[random%7]);}
        compare(directions,count,start,-1,i%4==0,i);
    }
    for(int count:{2,3,4,7,12})for(int start=0;start<count;++start)for(int failure=0;failure<20;++failure)compare(std::vector<int>(count,1),count,start,failure,false,19);
    // Short direction arrays can still complete an early native selection.
    for(int count:{4,7,12})compare({1,1,1},count,0);
    compare({1,1,1,1},4,2,-1,true);
    const auto changed=execute<ReferenceVobSegment>({1,1,1,1},4,2,-1,true,0);
    const auto unchanged=execute<ReferenceVobSegment>({1,1,1,1},4,2,-1,false,0);
    if(changed.output==unchanged.output || !changed.mutated || !interruptions || !assertions || !segmentHandled || !segmentFallback){fprintf(stderr,"VobSub segment route/append controls failed\n");return 1;}
    printf("%llu exact VobSub segment comparisons passed (%llu matching no-segment assertions, %llu injected append interruptions, %llu observed point/direction append attempts, %llu source mutations during native append, %llu Rust-selected segments, %llu native fallback selections; circular endpoints, signed midpoint rounding, partial paired arrays and live input after callbacks)\n",
        static_cast<unsigned long long>(comparisons),static_cast<unsigned long long>(assertions),static_cast<unsigned long long>(interruptions),static_cast<unsigned long long>(appends),static_cast<unsigned long long>(mutations),static_cast<unsigned long long>(segmentHandled),static_cast<unsigned long long>(segmentFallback));
}
