// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_wrap_width.h"
static uint64_t comparisons=0,observations=0,changed=0,excluded=0;
struct InputWord {int width;bool line;};
static bool fits(int64_t n){return n>=INT32_MIN && n<=INT32_MAX;}
// Preflight only: exclude expressions outside the pinned native arithmetic domain.
static bool defined(const std::vector<InputWord>& input,size_t start,int style,int maximum){
    if((style!=0 && style!=3) || maximum<=0)return true;
    int64_t full=0;for(size_t i=start;i<input.size();++i){if(input[i].line)break;full+=input[i].width;if(!fits(full))return false;}
    if(full==INT32_MIN)return false;
    int64_t denominator=(full<0?-full:full)/maximum+1;if(!fits(denominator))return false;
    int64_t minimum=full/denominator,width=0,last=0;
    for(size_t i=start;i<input.size() && width<minimum;++i){last=input[i].width;int64_t sum=width+last;if(!fits(sum) || sum==INT32_MIN)return false;if((sum<0?-sum:sum)<maximum)width=sum;}
    if(style==3 && width<full){int64_t rest=full-width;if(!fits(rest) || !fits(rest+last))return false;if(rest+last<maximum && !fits(width-last))return false;}
    return true;
}
static void seed(){errno=EDOM;
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
}
static unsigned long error(){
#ifdef _WIN32
    return GetLastError();
#else
    return 0;
#endif
}
static void compare(const std::vector<InputWord>& input,size_t start,int style,int maximum){
    if(!defined(input,start,style,maximum)){++excluded;return;}
    ReferenceWrap::CSubtitle a;CandidateWrap::CSubtitle b;a.m_wrapStyle=b.m_wrapStyle=style;
    for(size_t i=0;i<input.size();++i){SharedPtrCWord wa(new WrapWord{input[i].width,input[i].line,static_cast<int>(i)});SharedPtrCWord wb(new WrapWord(*wa));a.m_words.AddTail(wa);b.m_words.AddTail(wb);}
    POSITION pa=a.m_words.GetHeadPosition(),pb=b.m_words.GetHeadPosition();for(size_t i=0;i<start;++i){a.m_words.GetNext(pa);b.m_words.GetNext(pb);}
    wrapReads.clear();seed();int x=a.GetWrapWidth(pa,maximum);int ea=errno;unsigned long la=error();auto reads=wrapReads;
    wrapReads.clear();seed();int y=b.GetWrapWidth(pb,maximum);int eb=errno;unsigned long lb=error();
    if(x!=y || reads!=wrapReads || ea!=eb || la!=lb || a.m_wrapStyle!=b.m_wrapStyle){fprintf(stderr,"Wrap mismatch case=%llu count=%zu start=%zu style=%d max=%d result=%d/%d reads=%zu/%zu\n",(unsigned long long)comparisons,input.size(),start,style,maximum,x,y,reads.size(),wrapReads.size());std::abort();}
    ++comparisons;observations+=reads.size();changed+=x!=maximum;
}
int main(){
    const int widths[]={-20,-10,-1,0,1,4,5,9,10,11,20};
    for(int a:widths)for(int b:widths)for(int c:widths)for(unsigned mask=0;mask<8;++mask){std::vector<InputWord> input={{a,(mask&1)!=0},{b,(mask&2)!=0},{c,(mask&4)!=0}};for(int style:{0,3})for(int maximum:{1,5,10,20})compare(input,0,style,maximum);}
    uint32_t random=0x52a7bc91;auto next=[&](){random=random*1664525u+1013904223u;return random;};
    for(unsigned iteration=0;iteration<24000;++iteration){std::vector<InputWord> input;unsigned count=next()%33;for(unsigned i=0;i<count;++i){int width=static_cast<int>(next()%2001)-1000;bool line=(next()%7)==0;input.push_back({width,line});}size_t start=next()%(count+1);int style=static_cast<int>(next()%9)-2;int maximum=static_cast<int>(next()%2001)-200;compare(input,start,style,maximum);}
    for(int width:{INT32_MIN,INT32_MIN+1,-1,0,1,INT32_MAX-1,INT32_MAX})for(int other:{-1,0,1})for(int style:{INT32_MIN,-1,0,1,2,3,4,INT32_MAX})for(int maximum:{INT32_MIN,-1,0,1,2,INT32_MAX})for(unsigned start=0;start<4;++start)compare({{width,false},{other,false},{7,true}},start,style,maximum);
    compare({{12,false},{0,true},{3,false},{4,false}},0,0,10);
    compare({{12,false},{0,true},{3,false},{4,false}},0,3,10);
    if(!comparisons || !changed || !observations || !wrapBegins || !wrapSteps || !wrapFinishes || wrapDeclines || !excluded){fprintf(stderr,"Wrap route controls failed\n");return 1;}
    printf("%llu exact complete wrap-width comparisons passed (%llu changed results / %llu traversal observation values, %llu Rust initializations / %llu word steps / %llu completed results / %llu fallbacks; %llu arithmetic-domain exclusions, every wrap-style branch, negative/zero widths, strict width limit, rejected words, scanning beyond a line break and identifier limits; native font measurements and full-line accumulation retained)\n",(unsigned long long)comparisons,(unsigned long long)changed,(unsigned long long)observations,(unsigned long long)wrapBegins,(unsigned long long)wrapSteps,(unsigned long long)wrapFinishes,(unsigned long long)wrapDeclines,(unsigned long long)excluded);
}
