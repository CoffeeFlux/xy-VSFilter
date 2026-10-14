// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_line_layout.h"
static uint64_t comparisons=0,callbacks=0,interruptions=0,mutations=0,retained=0,changed=0;
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
static void compare(std::vector<CLine> lines,CSize size,CRect margin,int alignment,int stop=-1,int failure=-1,int mutation=0){
    for(size_t i=0;i<lines.size();++i)lines[i].id=static_cast<int>(i);
    ReferenceLineLayout::CSubtitle a;CandidateLineLayout::CSubtitle b;a.m_words.lines=b.m_words.lines=lines;a.m_scrAlignment=b.m_scrAlignment=alignment;
    lineStop=stop;lineFailure=failure;lineMutation=mutation;lineMutationCount=0;lineAttempts=0;lineEvents.clear();bool ia=false,ib=false;seed();
    try{a.MakeLines(size,margin);}catch(const LineInterrupted&){ia=true;}
    int ea=errno;unsigned long la=lastError();unsigned ma=lineMutationCount;auto events=lineEvents;auto state=a.snapshot();
    lineAttempts=0;lineMutationCount=0;lineEvents.clear();seed();try{b.MakeLines(size,margin);}catch(const LineInterrupted&){ib=true;}
    int eb=errno;unsigned long lb=lastError();
    if(ia!=ib || ea!=eb || la!=lb || ma!=static_cast<unsigned>(lineMutationCount) || state!=b.snapshot() || events!=lineEvents){fprintf(stderr,"Line layout mismatch case=%llu lines=%zu alignment=%d stop=%d failure=%d mutation=%d interruption=%d/%d events=%zu/%zu\n",(unsigned long long)comparisons,lines.size(),alignment,stop,failure,mutation,ia,ib,events.size(),lineEvents.size());std::abort();}
    ++comparisons;callbacks+=events.size();interruptions+=ia;mutations+=ma;retained+=a.output.size();changed+=a.m_rect.left!=91 || a.m_rect.top!=92 || a.m_rect.right!=93 || a.m_rect.bottom!=94;
}
int main(){
    for(int width:{-20,-1,0,1,20,79,80,81})for(int height:{-9,-1,0,1,9})for(int border:{-3,0,3})for(int alignment=-4;alignment<=13;++alignment)for(int count=0;count<4;++count){std::vector<CLine> lines;for(int i=0;i<count;++i)lines.push_back({width+i,height,height-1,border,border+1,0});compare(lines,CSize(81,63),CRect(1,2,4,5),alignment);}
    uint32_t random=0x8bc41d21;auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};auto signedNext=[&](){return static_cast<int>(next()%2001)-1000;};
    for(unsigned iteration=0;iteration<24000;++iteration){std::vector<CLine> lines;unsigned count=next()%17;for(unsigned i=0;i<count;++i)lines.push_back({signedNext(),signedNext(),signedNext(),signedNext(),signedNext(),0});CSize size(signedNext(),signedNext());CRect margin(signedNext(),signedNext(),signedNext(),signedNext());int alignment=static_cast<int>(next()%19)-5;int stop=static_cast<int>(next()%(count+2))-1;compare(lines,size,margin,alignment,stop);}
    const std::vector<CLine> lines={{17,3,2,2,4,0},{23,7,1,3,6,1},{11,2,4,1,8,2}};
    for(int alignment=-2;alignment<12;++alignment)for(int stop=-1;stop<4;++stop)for(int failure=-1;failure<8;++failure)for(int mutation=0;mutation<3;++mutation)compare(lines,CSize(81,63),CRect(1,2,4,5),alignment,stop,failure,mutation);
    // Extreme alignments do not participate in additive arithmetic.
    for(int alignment:{INT32_MIN,INT32_MAX})compare(lines,CSize(81,63),CRect(1,2,4,5),alignment);
    if(!comparisons || !callbacks || !interruptions || !mutations || !retained || !changed || !lineGrows || !linePlaces || lineDeclines){fprintf(stderr,"Line layout route controls failed\n");return 1;}
    printf("%llu exact complete subtitle-block layout comparisons passed (%llu retained lines / %llu callback observation values / %llu changed rectangles, %llu injected line-create/append interruptions / %llu live line/alignment mutations, %llu Rust extent updates / %llu placements / %llu fallbacks; signed alignment and centering, empty blocks, null line returns, partial appends, final border rereads, negative dimensions and margins; native line creation/insertion modeled and representable arithmetic only)\n",(unsigned long long)comparisons,(unsigned long long)retained,(unsigned long long)callbacks,(unsigned long long)changed,(unsigned long long)interruptions,(unsigned long long)mutations,(unsigned long long)lineGrows,(unsigned long long)linePlaces,(unsigned long long)lineDeclines);
}
