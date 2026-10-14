// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_cc.h"
#include <climits>
struct Outcome {
    std::vector<int64_t> events,state;std::vector<Caption> captions;std::vector<BYTE> source;bool interrupted,mutated;int attempts;
    bool operator==(const Outcome& o)const{return events==o.events && state==o.state && captions==o.captions && source==o.source && interrupted==o.interrupted && mutated==o.mutated && attempts==o.attempts;}
};
static Outcome* active=nullptr;static BYTE* activeSource=nullptr;static size_t activeLength=0;static int failureIndex=-1,mutationMode=0;
template<class Decoder> struct Context {static Decoder* decoder;};
template<class Decoder> Decoder* Context<Decoder>::decoder=nullptr;
template<class Decoder> static void state(std::vector<int64_t>& out,const Decoder& decoder){
    out.insert(out.end(),{decoder.m_time,decoder.m_fEndOfCaption,decoder.m_cursor.x,decoder.m_cursor.y});
    for(const auto& row:decoder.m_buff)for(WCHAR c:row)out.push_back(c);
    for(const auto& row:decoder.m_disp)for(WCHAR c:row)out.push_back(c);
}
template<class Decoder> static void seed(Decoder& decoder,unsigned value){
    decoder.m_time=int(value%10000)-5000;decoder.m_fEndOfCaption=(value&1)!=0;decoder.m_cursor=CPoint(value%32,(value/32)%16);
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x){decoder.m_buff[y][x]=(value+x+y)%7?WCHAR('A'+(x+y)%26):0;decoder.m_disp[y][x]=(value+x+y)%11?WCHAR('a'+(x+y)%26):0;}
}
template<class Decoder> static void observe(const Caption& caption){
    auto& decoder=*Context<Decoder>::decoder;
    active->events.insert(active->events.end(),{caption.unicode,caption.start,caption.end,int64_t(caption.text.size())});for(uint16_t c:caption.text)active->events.push_back(c);state(active->events,decoder);
    if(active->attempts++==failureIndex)throw CaptionInterrupted();
    if(!active->mutated && mutationMode){
        if(mutationMode==1){decoder.m_buff[0][0]=WCHAR('Z');decoder.MoveCursor(31,15);decoder.m_time=-777;decoder.m_fEndOfCaption=!decoder.m_fEndOfCaption;}
        if(mutationMode==2 && activeLength)activeSource[0]^=0x44;
        active->mutated=true;
    }
}
template<class Decoder> static Outcome decode(const std::vector<BYTE>& input,int length,unsigned initial,int alias,int failure,int mutation){
    Decoder decoder;seed(decoder,initial);Outcome result={};std::vector<BYTE> source=input;BYTE* data=source.data();
    if(alias){data=reinterpret_cast<BYTE*>(alias==1?decoder.m_buff:decoder.m_disp);if(!input.empty())std::memcpy(data,input.data(),input.size());}
    active=&result;Context<Decoder>::decoder=&decoder;activeSource=data;activeLength=input.size();failureIndex=failure;mutationMode=mutation;ccAddHook=&observe<Decoder>;
    try{decoder.DecodeCC(data,length,12345);}catch(const CaptionInterrupted&){result.interrupted=true;}
    state(result.state,decoder);result.captions=decoder.m_sts.entries;if(!input.empty())result.source.assign(data,data+input.size());ccAddHook=nullptr;active=nullptr;return result;
}
static uint64_t comparisons=0,cursors=0,cellWrites=0,captions=0,interruptions=0,mutations=0,aliases=0;
static void compare(const std::vector<BYTE>& input,unsigned initial=0,int alias=0,int failure=-1,int mutation=0,int length=-1){
    if(length==-1)length=int(input.size());const auto a=decode<ReferenceCC::CCDecoder>(input,length,initial,alias,failure,mutation),b=decode<CandidateCC::CCDecoder>(input,length,initial,alias,failure,mutation);
    if(!(a==b)){fprintf(stderr,"Caption decode mismatch case=%llu length=%d initial=%u alias=%d failure=%d mutation=%d\n",(unsigned long long)comparisons,length,initial,alias,failure,mutation);std::abort();}
    ++comparisons;captions+=a.captions.size();interruptions+=a.interrupted;mutations+=a.mutated;aliases+=alias!=0;
}
int main(){
    for(int x:{INT_MIN,-100,-1,0,1,30,31,32,33,64,INT_MAX})for(int y:{INT_MIN,-100,-1,0,1,14,15,16,17,100,INT_MAX}){
        if(x>=32 && y==INT_MAX)continue;ReferenceCC::CCDecoder a;CandidateCC::CCDecoder b;a.MoveCursor(x,y);b.MoveCursor(x,y);if(a.m_cursor.x!=b.m_cursor.x || a.m_cursor.y!=b.m_cursor.y)std::abort();++cursors;
    }
    for(unsigned unit=0;unit<=0xffff;++unit){ReferenceCC::CCDecoder a;CandidateCC::CCDecoder b;seed(a,unit);seed(b,unit);a.PutChar(WCHAR(unit));b.PutChar(WCHAR(unit));std::vector<int64_t> as,bs;state(as,a);state(bs,b);if(as!=bs)std::abort();++cellWrites;}
    compare({});compare({},0,0,-1,0,INT_MIN);
    for(unsigned first=0;first<256;++first){compare({BYTE(first)},first);for(unsigned second=0;second<256;++second)compare({BYTE(first),BYTE(second)},first*256+second);}
    for(unsigned first=0;first<256;++first)for(unsigned second=0;second<256;++second)compare({BYTE(first),BYTE(second),BYTE(first),BYTE(second)},first+second);
    uint32_t random=0x84917326;
    for(unsigned value=0;value<12000;++value){std::vector<BYTE> input;for(unsigned i=0;i<value%64;++i){random^=random<<13;random^=random>>17;random^=random<<5;input.push_back(BYTE(random));}compare(input,random,value%3);}
    const std::vector<BYTE> samples[]={{0x94,0x2f},{0x94,0x2c},{0x94,0x2f,0x94,0x2f,'A',0x94,0x2c},{0x91,0x39,0x97,0xa2,'A',0x94,0x2f,'B',0x94,0x2f,0x94,0x2c}};
    for(const auto& input:samples)for(unsigned initial:{0u,1u,511u})for(int alias=0;alias<3;++alias)for(int mutation=0;mutation<3;++mutation)for(int failure=-1;failure<5;++failure)compare(input,initial,alias,failure,mutation);
    if(!captions || !interruptions || !mutations || !aliases)return 1;
    printf("%llu exact native caption decodes, %llu cursor moves and %llu cell writes passed (%llu emitted subtitle records, %llu injected Add interruptions, %llu callback mutations, %llu input/backing aliases; every byte pair, doubled controls, every UTF-16 value, 33-cell row layout and full display/buffer state; raw file I/O and unrepresentable arithmetic excluded)\n",(unsigned long long)comparisons,(unsigned long long)cursors,(unsigned long long)cellWrites,(unsigned long long)captions,(unsigned long long)interruptions,(unsigned long long)mutations,(unsigned long long)aliases);
}
