// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_text_seek.h"
static uint64_t comparisons=0,callbacks=0,interruptions=0,mutations=0,excluded=0;
static void seed(){errno=EDOM;
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
}
static unsigned long lastError(){
#ifdef _WIN32
    return GetLastError();
#else
    return 0;
#endif
}
static bool defined(uint64_t nativePosition,uint64_t nativeLength,int bom,size_t count,size_t cursor,int64_t offset,unsigned origin,int mutation){
    if(mutation&1){bom=(bom+1)%4;count=7;cursor=3;++nativePosition;++nativeLength;}
    uint64_t position=nativePosition-static_cast<uint64_t>(bom)-(count?count-cursor:0);
    if(mutation&2){bom=(bom+1)%4;nativeLength+=2;}
    uint64_t length=nativeLength-static_cast<uint64_t>(bom);
    uint64_t raw=origin==1?position+static_cast<uint64_t>(offset):origin==2?length-static_cast<uint64_t>(offset):static_cast<uint64_t>(offset);
    uint64_t clamped=raw<length?raw:length;
    if(clamped>INT64_MAX)clamped=0;
    return clamped<=static_cast<uint64_t>(INT64_MAX)-static_cast<unsigned>(bom);
}
static void compare(uint64_t nativePosition,uint64_t nativeLength,int bom,size_t count,size_t cursor,int64_t offset,unsigned origin,int failure=-1,int mutation=0,bool partial=false){
    if(!defined(nativePosition,nativeLength,bom,count,cursor,offset,origin,mutation)){++excluded;return;}
    ReferenceSeek::CTextFile a;CandidateSeek::CTextFile b;
    for(NativeSeekFile* f:{static_cast<NativeSeekFile*>(&a),static_cast<NativeSeekFile*>(&b)}){f->nativePosition=nativePosition;f->nativeLength=nativeLength;f->m_offset=bom;f->m_bufferCount=count;f->m_bufferPos=cursor;}
    seekFailure=failure;seekMutation=mutation;seekPartial=partial;seekAttempts=0;seekMutations=0;seekEvents.clear();bool ia=false,ib=false;uint64_t x=37,y=37;seed();
    try{x=a.Seek(offset,origin);}catch(const SeekInterrupted&){ia=true;}
    int ea=errno;unsigned long la=lastError();unsigned ma=seekMutations;auto events=seekEvents;auto state=a.snapshot();
    seekAttempts=0;seekMutations=0;seekEvents.clear();seed();try{y=b.Seek(offset,origin);}catch(const SeekInterrupted&){ib=true;}
    int eb=errno;unsigned long lb=lastError();
    if(x!=y || ia!=ib || ea!=eb || la!=lb || ma!=static_cast<unsigned>(seekMutations) || state!=b.snapshot() || events!=seekEvents){fprintf(stderr,"Text seek mismatch case=%llu position=%llu length=%llu bom=%d offset=%lld origin=%u failure=%d mutation=%d partial=%d result=%llu/%llu\n",(unsigned long long)comparisons,(unsigned long long)nativePosition,(unsigned long long)nativeLength,bom,(long long)offset,origin,failure,mutation,partial,(unsigned long long)x,(unsigned long long)y);std::abort();}
    ++comparisons;callbacks+=events.size();interruptions+=ia;mutations+=ma;
}
int main(){
    const uint64_t positions[]={0,1,2,3,9,4096,INT64_MAX-3,INT64_MAX,uint64_t(1)<<63,UINT64_MAX-1,UINT64_MAX};
    const int64_t offsets[]={INT64_MIN,INT64_MIN+1,-4096,-3,-1,0,1,3,4096,INT64_MAX-1,INT64_MAX};
    for(uint64_t position:positions)for(uint64_t length:positions)for(int64_t offset:offsets)for(unsigned origin:{0u,1u,2u,3u,UINT32_MAX})for(int bom=0;bom<4;++bom)for(unsigned buffer=0;buffer<4;++buffer){const size_t count[]={0,1,7,4096},cursor[]={5,0,3,4095};compare(position,length,bom,count[buffer],cursor[buffer],offset,origin);}
    for(uint64_t position:{0ull,9ull,4096ull})for(uint64_t length:{0ull,17ull,4096ull})for(int64_t offset:{-20ll,-1ll,0ll,9ll,100ll})for(unsigned origin=0;origin<4;++origin)for(int failure=-1;failure<4;++failure)for(int mutation=0;mutation<8;++mutation)for(int partial=0;partial<2;++partial)compare(position,length,3,7,3,offset,origin,failure,mutation,partial!=0);
    if(!comparisons || !callbacks || !interruptions || !mutations || !seekHandled || seekDeclined || !excluded){fprintf(stderr,"Text seek route controls failed\n");return 1;}
    printf("%llu exact complete text-file seek comparisons passed (%llu callback observation values / %llu injected interruptions / %llu live file-state mutations, %llu Rust targets / %llu fallbacks; %llu unrepresentable physical-offset exclusions, all origins/unknown origins, unsigned wrap and signed clamp, BOM and buffered offsets, native failure before/after mutation, buffer invalidation only after successful seek; native OS file operations modeled)\n",(unsigned long long)comparisons,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)mutations,(unsigned long long)seekHandled,(unsigned long long)seekDeclined,(unsigned long long)excluded);
}
