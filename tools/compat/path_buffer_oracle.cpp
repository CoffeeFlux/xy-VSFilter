// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_path_buffer.h"
static uint64_t comparisons=0,continuations=0,bytes=0,callbacks=0,appended=0,retries=0;
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
static void compare(int originalCount,int newCount,bool closeResult,bool endResult,int fillMode,int allocationFailure,PathOffset dx,PathOffset dy,int pointMode=0){
    pathAllocationFailure=0;pathAllocationCalls=0;ReferencePathBuffer::PathData a(originalCount);CandidatePathBuffer::PathData b(originalCount);
    for(int attempt=0;attempt<2;++attempt){
        PathContext ar(newCount),br(newCount);ar.closeResult=br.closeResult=closeResult;ar.endResult=br.endResult=attempt?true:endResult;ar.fillMode=br.fillMode=attempt?0:fillMode;
        for(int i=0;i<newCount;++i){
            POINT point=ar.points[i];
            if(pointMode==1){point.x=INT32_MIN+4096+i;point.y=INT32_MAX-4096-i;}
            if(pointMode==2){point.x=INT32_MAX-4096-i;point.y=INT32_MIN+4096+i;}
            if(pointMode==3){point.x=i;point.y=-i;}
            if(pointMode==4){point.x=-i;point.y=i;}
            ar.points[i]=br.points[i]=point;
        }
        pathAllocationFailure=attempt?0:allocationFailure;pathAllocationCalls=0;pathAllocationEvents.clear();pathContext=&ar;
        seed();bool ra=a.PartialEndPath(NULL,dx,dy);Effects ea;auto allocations=pathAllocationEvents;auto state=a.snapshot();if(livePathBuffers)std::abort();
        pathAllocationCalls=0;pathAllocationEvents.clear();pathContext=&br;
        seed();bool rb=b.PartialEndPath(NULL,dx,dy);Effects eb;
        if(ra!=rb||!(ea==eb)||allocations!=pathAllocationEvents||state!=b.snapshot()||ar.events!=br.events||ar.breaks!=br.breaks||livePathBuffers){fprintf(stderr,"Path buffer mismatch old=%d new=%d close=%d end=%d mode=%d alloc=%d attempt=%d\n",originalCount,newCount,closeResult,endResult,fillMode,allocationFailure,attempt);std::abort();}
        // Native DebugBreak is not executed here. A modeled continuation proves
        // cleanup order only and never counts as a native compatibility pass.
        if(ar.breaks)++continuations;else{++comparisons;bytes+=state.size();callbacks+=ar.events.size();appended+=ra?newCount:0;}
        retries+=attempt;
    }
}
int main(){
    for(int old:{0,1,3,8})for(int count:{0,1,2,3,8,31})for(bool close:{false,true})for(bool end:{false,true})for(int mode=0;mode<4;++mode)for(int failure=0;failure<3;++failure)for(int offset:{-512,-1,0,1,512})compare(old,count,close,end,mode,failure,offset,-offset);
    for(int old:{0,1,3})for(int count:{1,2,256})for(int mode:{1,2})for(int offset:{-4096,-1,0,1,4096})compare(old,count,true,true,0,0,offset,-offset,mode);
    for(int old:{0,3})for(int count:{1,256}){
        compare(old,count,true,true,0,0,INT32_MIN,INT32_MAX,3);
        compare(old,count,true,true,0,0,INT32_MAX,INT32_MIN,4);
    }
    uint32_t random=0x87a132b9;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned i=0;i<8000;++i)compare(next()%33,next()%65,next()%2,next()%2,next()%4,next()%3,static_cast<int>(next()%4097)-2048,static_cast<int>(next()%4097)-2048);
    if(!pathRustAppends||pathRustAppendDeclines)std::abort();
    printf("%llu Rust translated path appends passed with %llu native arithmetic fallbacks (writes initialize native destination tails; source loans end before Rust reads)\n",(unsigned long long)pathRustAppends,(unsigned long long)pathRustAppendDeclines);
    if(!comparisons||!continuations||!appended||!pathRustOwners||pathRustOwners!=pathRustLoans||pathRustLoans!=pathRustReleases||pathRustFallbacks||!pathAllocations.empty()||livePathBuffers)std::abort();
    printf("%llu exact non-break PartialEndPath comparisons passed (%llu buffer byte/metadata fields / %llu callback observations / %llu appended points / %llu retries; %llu additional modeled DebugBreak continuations are NOT native parity passes). %llu Rust temporary owners / %llu paired output loans / %llu releases / %llu fallbacks cover both categories; native realloc ordering and partial metadata, complete/short/error GDI output, cleanup before AbortPath, errno/FP/Windows error state; GDI output-only and allocation boundaries modeled, native traps/OOM/negative counts/unrepresentable geometry excluded\n",(unsigned long long)comparisons,(unsigned long long)bytes,(unsigned long long)callbacks,(unsigned long long)appended,(unsigned long long)retries,(unsigned long long)continuations,(unsigned long long)pathRustOwners,(unsigned long long)pathRustLoans,(unsigned long long)pathRustReleases,(unsigned long long)pathRustFallbacks);
}
