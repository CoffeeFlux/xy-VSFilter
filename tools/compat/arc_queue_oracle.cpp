// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_arc_updates.h"
static uint64_t histories=0,operations=0,observations=0,updates=0,callbacks=0,fullRejects=0,wraps=0;
static uint32_t randomState=0x7e4ad389;
static uint32_t next(){randomState^=randomState<<13;randomState^=randomState>>17;randomState^=randomState<<5;return randomState;}
static void fail(const char* what){fprintf(stderr,"Arc queue mismatch: %s at history %llu operation %llu update %llu\n",what,(unsigned long long)histories,(unsigned long long)operations,(unsigned long long)updates);std::abort();}
static LinkArcItem item(uint64_t center,int deadline){LinkArcItem v;v.arc_center=center;v.dead_line=deadline;return v;}
static void append(ReferenceArcs::LinkArc& q,const LinkArcItem& v){q.inc_1_at_tail()=v;}
static void append(CandidateArcs::LinkArc& q,const LinkArcItem& v){q.append(v.arc_center,v.dead_line);}
static void center(ReferenceArcs::LinkArc& q,int i,uint64_t c){q.get_at(i).arc_center=c;}
static void center(CandidateArcs::LinkArc& q,int i,uint64_t c){q.set_center(i,c);}
static void deadline(ReferenceArcs::LinkArc& q,int i,int d){q.get_at(i).dead_line=d;}
static void deadline(CandidateArcs::LinkArc& q,int i,int d){q.set_deadline(i,d);}
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
struct Effects {
    int error,flags; unsigned long last;
    Effects():error(errno),flags(std::fetestexcept(FE_ALL_EXCEPT)),last(0){
#ifdef _WIN32
last=GetLastError();
#endif
    }
    bool operator==(const Effects& o)const{return error==o.error&&flags==o.flags&&last==o.last;}
};
static void compare(ReferenceArcs::LinkArc& a,CandidateArcs::LinkArc& b){
    if(a.capacity()!=b.capacity()||a.size()!=b.size()||a.free()!=b.free()||a.full()!=b.full()||a.empty()!=b.empty())fail("metadata");
    for(size_t i=0;i<a.capacity();++i){auto x=a.get_at(static_cast<int>(i)),y=b.get_at(static_cast<int>(i));if(x.arc_center!=y.arc_center||x.dead_line!=y.dead_line)fail("slot");observations+=2;}
    auto x=a.back(),y=b.back();if(x.arc_center!=y.arc_center||x.dead_line!=y.dead_line)fail("back");
}
template<class Queue> static int edit(Queue& q,int op,int index,const LinkArcItem& value){
    switch(op){case 0:append(q,value);break;case 1:return q.push_back(value);case 2:q.pop_front();break;case 3:q.pop_back();break;case 4:q.pop_last_n(index);break;case 5:center(q,index,value.arc_center);break;case 6:deadline(q,index,value.dead_line);break;}return 0;
}
int main(){
    for(int capacity: {1,2,3,4,6,8,16,34,66,130})for(unsigned h=0;h<128;++h){
        ReferenceArcs::LinkArc a;CandidateArcs::LinkArc b;a.init(capacity);b.init(capacity);compare(a,b);
        for(unsigned step=0;step<512;++step){
            int op=static_cast<int>(next()%7),index=static_cast<int>(next()%(capacity+3));uint64_t c=(uint64_t(next())<<32)|next();int d=static_cast<int32_t>(next());
            LinkArcItem v=item(c,d);bool wasNonempty=!a.empty();seed();int ra=edit(a,op,index,v);Effects ea;seed();int rb=edit(b,op,index,v);Effects eb;
            if(ra!=rb||!(ea==eb))fail("return/effects");fullRejects+=ra==-1;wraps+=op==0&&wasNonempty&&a.empty();compare(a,b);++operations;
        }++histories;
    }
    // Complete pinned arc update methods; controlled native ellipse replies
    // allow every branch without changing float calculations in production.
    for(unsigned h=0;h<24000;++h){
        int ry=1+next()%32,capacity=2*ry+2;
        ReferenceArcs::LinkArc a;CandidateArcs::LinkArc b;a.init(capacity);b.init(capacity);
        unsigned count=1+next()%(capacity-1);for(unsigned i=0;i<count;++i){LinkArcItem v=item((uint64_t(64+i)<<32)|(next()%1024),96+next()%64);append(a,v);append(b,v);}
        for(unsigned step=0;step<12;++step){
            if(a.empty()){LinkArcItem v=item((uint64_t(64)<<32)|42,128);append(a,v);append(b,v);}
            ArcEllipse ar(ry),br(ry);for(int i=0;i<capacity+2;++i)ar.replies.push_back(static_cast<int>(next()%(2*ry+3))-ry-1);br.replies=ar.replies;
            ReferenceArcs::WidenRegionCreaterImpl ref(&ar);CandidateArcs::WidenRegionCreaterImpl candidate(&br);
            uint64_t c=(uint64_t(100+step)<<32)|(next()%1024);int op=next()%3,ra=0,rb=0;
            // Cross-left's sentinel early returns have no signed addition.
            if(op==2&&step%3==0){ar.replies[0]=br.replies[0]=INT32_MAX;}else if(op==2&&step%3==1){ar.replies[1]=br.replies[1]=INT32_MIN;}
            seed();if(op==0)ref.add_left(&a,c);else if(op==1)ref.add_right(&a,c);else ra=ref.cross_left(a,c);Effects ea;
            seed();if(op==0)candidate.add_left(&b,c);else if(op==1)candidate.add_right(&b,c);else rb=candidate.cross_left(b,c);Effects eb;
            if(ra!=rb||!(ea==eb)||ar.calls!=br.calls||ar.cursor!=br.cursor)fail("whole update");
            callbacks+=ar.cursor;compare(a,b);++updates;
        }
    }
    if(!fullRejects||!wraps||!callbacks)fail("coverage");
    printf("%llu exact arc-queue histories / %llu operations / %llu physical slot fields and %llu complete arc updates passed (%llu native ellipse callbacks / %llu full rejections / %llu tail wraps; retained removed slots, capacity-minus-one full rule, copied scalar records, callback order, errno/FP/Windows error state; positive initialized capacities, nonnegative representable indices/counts and successful allocation; ellipse replies modeled, production float math retained)\n",(unsigned long long)histories,(unsigned long long)operations,(unsigned long long)observations,(unsigned long long)updates,(unsigned long long)callbacks,(unsigned long long)fullRejects,(unsigned long long)wraps);
}
