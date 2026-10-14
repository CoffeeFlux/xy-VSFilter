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
    if(value==UINT_MAX)return;
    decoder.m_time=int(value%10000)-5000;decoder.m_fEndOfCaption=(value&1)!=0;decoder.m_cursor=CPoint(value%32,(value/32)%16);
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<32;++x){decoder.m_buff[y][x]=(value+x+y)%7?WCHAR('A'+(x+y)%26):0;decoder.m_disp[y][x]=(value+x+y)%11?WCHAR('a'+(x+y)%26):0;}
    if(value==UINT_MAX-1){decoder.m_fEndOfCaption=true;std::memcpy(decoder.m_disp,decoder.m_buff,sizeof(decoder.m_disp));}
}
template<class Decoder> static void observe(const Caption& caption){
    auto& decoder=*Context<Decoder>::decoder;
    active->events.insert(active->events.end(),{caption.unicode,caption.start,caption.end,int64_t(caption.text.size())});for(uint16_t c:caption.text)active->events.push_back(c);state(active->events,decoder);
    if(active->attempts++==failureIndex)throw CaptionInterrupted();
    if(!active->mutated && mutationMode){
        if(mutationMode==1){decoder.m_buff[0][0]=WCHAR('Z');decoder.m_disp[0][0]=WCHAR('Y');decoder.MoveCursor(31,15);decoder.m_time=-777;decoder.m_fEndOfCaption=!decoder.m_fEndOfCaption;}
        if(mutationMode==2 && activeLength)activeSource[0]^=0x44;
        active->mutated=true;
    }
}
template<class Decoder> static Outcome decode(const std::vector<BYTE>& input,int length,unsigned initial,int alias,int failure,int mutation,const std::vector<std::vector<BYTE>>* tail){
    Decoder decoder;seed(decoder,initial);Outcome result={};active=&result;Context<Decoder>::decoder=&decoder;failureIndex=failure;mutationMode=mutation;ccAddHook=&observe<Decoder>;
    int64_t time=12345;
    const auto step=[&](const std::vector<BYTE>& bytes,int count){
        std::vector<BYTE> source=bytes;BYTE* data=source.data();
        if(alias){data=reinterpret_cast<BYTE*>(alias==1?decoder.m_buff:decoder.m_disp);if(!bytes.empty())std::memcpy(data,bytes.data(),bytes.size());}
        activeSource=data;activeLength=bytes.size();result.events.push_back(time);
        try{decoder.DecodeCC(data,count,time);}catch(const CaptionInterrupted&){result.interrupted=true;}
        state(result.state,decoder);if(!bytes.empty())result.source.insert(result.source.end(),data,data+bytes.size());time+=1000;
    };
    step(input,length);if(tail)for(const auto& bytes:*tail)step(bytes,int(bytes.size()));
    result.captions=decoder.m_sts.entries;ccAddHook=nullptr;active=nullptr;return result;
}
static uint64_t comparisons=0,decodeCalls=0,cursors=0,cellWrites=0,captions=0,interruptions=0,mutations=0,aliases=0;
static void compare(const std::vector<BYTE>& input,unsigned initial=0,int alias=0,int failure=-1,int mutation=0,int length=-1,const std::vector<std::vector<BYTE>>* tail=nullptr){
    if(length==-1)length=int(input.size());const auto a=decode<ReferenceCC::CCDecoder>(input,length,initial,alias,failure,mutation,tail),b=decode<CandidateCC::CCDecoder>(input,length,initial,alias,failure,mutation,tail);
    if(!(a==b)){fprintf(stderr,"Caption decode mismatch case=%llu length=%d initial=%u alias=%d failure=%d mutation=%d\n",(unsigned long long)comparisons,length,initial,alias,failure,mutation);std::abort();}
    ++comparisons;decodeCalls+=1+(tail?tail->size():0);captions+=a.captions.size();interruptions+=a.interrupted;mutations+=a.mutated;aliases+=alias!=0;
}
static uint64_t displayCases=0,displayUnits=0,displayInterruptions=0,displayMutations=0;
template<class Decoder> static Outcome saveDisplay(const std::vector<WCHAR>& cells,int64_t start,int64_t end,int failure,int mutation){
    Decoder decoder;seed(decoder,511);decoder.m_time=start;std::memcpy(decoder.m_disp,cells.data(),sizeof(decoder.m_disp));
    Outcome result={};active=&result;Context<Decoder>::decoder=&decoder;activeSource=nullptr;activeLength=0;failureIndex=failure;mutationMode=mutation;ccAddHook=&observe<Decoder>;
    try{decoder.SaveDisp(end);}catch(const CaptionInterrupted&){result.interrupted=true;}
    state(result.state,decoder);result.captions=decoder.m_sts.entries;ccAddHook=nullptr;active=nullptr;return result;
}
static void compareDisplay(const std::vector<WCHAR>& cells,int64_t start=9876543210LL,int64_t end=-9876543210LL,int failure=-1,int mutation=0){
    const auto a=saveDisplay<ReferenceCC::CCDecoder>(cells,start,end,failure,mutation),b=saveDisplay<CandidateCC::CCDecoder>(cells,start,end,failure,mutation);
    if(!(a==b)){fprintf(stderr,"Caption display mismatch case=%llu failure=%d mutation=%d\n",(unsigned long long)displayCases,failure,mutation);std::abort();}
    ++displayCases;displayInterruptions+=a.interrupted;displayMutations+=a.mutated;for(const auto& caption:a.captions)displayUnits+=caption.text.size();
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
    for(const auto& input:samples)for(unsigned initial:{0u,1u,511u,UINT_MAX,UINT_MAX-1})for(int alias=0;alias<3;++alias)for(int mutation=0;mutation<3;++mutation)for(int failure=-1;failure<5;++failure)compare(input,initial,alias,failure,mutation);
    const std::vector<BYTE> fragments[]={{0x94,0xae},{'A'},{0x94,0x2f},{0x94,0x2c},{0x91,0x39},{0x92,0x20},{0x13,0x3f},{0x94,0x2f,0x94,0x2f},{0x80},{'A','B','C'},{0x97,0xa1},{0x94},{0x2f}};
    for(unsigned value=0;value<5000;++value){std::vector<std::vector<BYTE>> tail;for(unsigned i=0;i<value%9;++i){random^=random<<13;random^=random>>17;random^=random<<5;tail.push_back(fragments[random%13]);}compare(fragments[value%13],value%2?UINT_MAX:UINT_MAX-1,value%3,-1,value%11==0?1:0,-1,&tail);}
    const std::vector<BYTE> packet={0x94,0xae,'A',0x94,0x2f,'B',0x94,0x2f,0x94,0x2c};
    for(size_t split=0;split<=packet.size();++split)for(int alias=0;alias<3;++alias)for(unsigned initial:{UINT_MAX,UINT_MAX-1})for(int failure=-1;failure<4;++failure){
        const std::vector<BYTE> first(packet.begin(),packet.begin()+split);const std::vector<std::vector<BYTE>> tail={std::vector<BYTE>(packet.begin()+split,packet.end()),{0x94,0x2f},{0x94,0x2c}};
        compare(first,initial,alias,failure,0,-1,&tail);
    }
    for(unsigned unit=0;unit<=0xffff;++unit){std::vector<WCHAR> cells(528,0);const unsigned cell=unit%512;cells[(cell/32)*33+cell%32]=WCHAR(unit);compareDisplay(cells,int64_t(unit)*65537-2147483648LL,12345);}
    for(unsigned gap=0;gap<528;++gap){std::vector<WCHAR> cells(528,WCHAR('Q'));cells[527]=0;cells[gap]=0;compareDisplay(cells);}
    for(unsigned value=0;value<8000;++value){std::vector<WCHAR> cells(528,0);for(unsigned i=0;i<527;++i){random^=random<<13;random^=random>>17;random^=random<<5;cells[i]=random%7?WCHAR(random&0xffff):0;}compareDisplay(cells);}
    for(unsigned pattern=0;pattern<4;++pattern){std::vector<WCHAR> cells(528,0);for(unsigned i=0;i<527;++i){if(pattern==1)cells[i]=WCHAR('A');if(pattern==2 && i%3==0)cells[i]=WCHAR(0xd800);if(pattern==3 && i%33==32)cells[i]=WCHAR('X');}
        for(int64_t start:{LLONG_MIN,0LL,LLONG_MAX})for(int64_t end:{LLONG_MIN,0LL,LLONG_MAX})for(int failure=-1;failure<2;++failure)for(int mutation=0;mutation<2;++mutation)compareDisplay(cells,start,end,failure,mutation);
    }
    // Candidate-only decline control: no native comparison reads beyond the
    // declared initialized display when no terminator can be found there.
    uint16_t allNonzero[528];for(auto& value:allNonzero)value=1;
    void* owner=xy_vsfilter_cc_text_create(allNonzero);if(xy_vsfilter_cc_text_field(owner,0,0)!=0)return 1;xy_vsfilter_cc_text_release(owner);
    if(!captions || !interruptions || !mutations || !aliases || !ccTextHandled || ccTextFallback || !displayUnits || !displayInterruptions || !displayMutations)return 1;
    printf("%llu exact SaveDisp comparisons passed (%llu emitted UTF-16 units, %llu injected Add interruptions, %llu callback mutations, %llu Rust text selections, %llu native fallbacks; row gaps, bounded cross-row strings, every UTF-16 value, native timestamp truncation and input state; unterminated scans beyond declared backing excluded)\n",(unsigned long long)displayCases,(unsigned long long)displayUnits,(unsigned long long)displayInterruptions,(unsigned long long)displayMutations,(unsigned long long)ccTextHandled,(unsigned long long)ccTextFallback);
    printf("%llu exact caption sequences / %llu DecodeCC calls, %llu cursor moves and %llu cell writes passed (%llu emitted subtitle records, %llu injected Add interruptions, %llu callback mutations, %llu input/backing aliases; every byte pair, doubled controls, split/repeated calls, empty/equal displays, every UTF-16 value, 33-cell row layout and full display/buffer state; raw file I/O and unrepresentable arithmetic excluded)\n",(unsigned long long)comparisons,(unsigned long long)decodeCalls,(unsigned long long)cursors,(unsigned long long)cellWrites,(unsigned long long)captions,(unsigned long long)interruptions,(unsigned long long)mutations,(unsigned long long)aliases);
}
