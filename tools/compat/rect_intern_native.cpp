// SPDX-License-Identifier: GPL-2.0-or-later
// Actual cleanup FwRect methods, ownership and aliasing; no copied interner.
#include <stdafx.h>
#include <flyweight_base_types.h>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <vector>
#include <exception>

struct LateRect {
    std::shared_ptr<FwRect> value;
    ~LateRect() {
        if(!value)return;
        if(value->get()!=CRect(123,-456,789,-123))std::terminate();
        value.reset();
        printf("rect-shutdown count=%llu\n",static_cast<unsigned long long>(FwRect::Count()));
    }
};
static LateRect late;
static std::shared_ptr<FwRect> held[16];
static unsigned step=0;
static void observe(unsigned op,unsigned slot) {
    printf("rect-step index=%u op=%u slot=%u count=%llu\n",step++,op,slot,static_cast<unsigned long long>(FwRect::Count()));
    for(unsigned i=0;i<16;++i) {
        unsigned aliases=0;
        if(held[i])for(unsigned j=0;j<16;++j)if(held[j] && *held[i]==*held[j])aliases|=1u<<j;
        const CRect value=held[i]?held[i]->get():CRect();
        printf("rect-held slot=%u present=%u aliases=%04x value=%ld,%ld,%ld,%ld\n",i,held[i]?1:0,aliases,value.left,value.top,value.right,value.bottom);
    }
}
static CRect fixture(unsigned i) {
    const int values[]={0,1,-1,20,INT32_MAX,INT32_MIN,123456789,-987654321};
    return CRect(values[i%8],values[(i/3)%8],values[(i/7)%8],values[(i/11)%8]);
}
int main() {
    try {
        printf("rect-provenance pointer_bits=%u ansi=%u oem=%u\n",unsigned(sizeof(void*)*8),GetACP(),GetOEMCP());
        observe(8,0);
        for(unsigned i=0;i<256;++i) {
            unsigned at=(i*7)%16,from=(i*11+1)%16,op=i%8;
            switch(op) {
                case 0:held[at]=std::make_shared<FwRect>(fixture(i));break;
                case 1:held[at]=std::make_shared<FwRect>();break;
                case 2:held[at]=held[from]?std::make_shared<FwRect>(*held[from]):std::make_shared<FwRect>();break;
                case 3:if(!held[at])held[at]=std::make_shared<FwRect>();*held[at]=fixture(i);break;
                case 4:if(!held[at])held[at]=std::make_shared<FwRect>();if(held[from])*held[at]=*held[from];break;
                case 5:held[at].reset();break;
                case 6:if(!held[at])held[at]=std::make_shared<FwRect>();*held[at]=*held[at];break;
                case 7:held[at]=std::make_shared<FwRect>(held[from]?held[from]->get():CRect());break;
            }
            observe(op,at);
        }
        for(unsigned i=0;i<16;++i){held[i].reset();observe(9,i);}
        std::vector<std::shared_ptr<FwRect>> bulk;
        for(int i=0;i<2048;++i) {
            bulk.push_back(std::make_shared<FwRect>(CRect(i,-i,i*33,i*7)));
            if(i%128==0)observe(10,unsigned(i));
        }
        observe(11,0);
        held[0]=std::make_shared<FwRect>(*bulk[0]);observe(12,0);
        bulk.clear();observe(13,0);
        held[0].reset();observe(14,0);
        late.value=std::make_shared<FwRect>(CRect(123,-456,789,-123));observe(15,0);
        printf("rect-complete steps=%u\n",step);return 0;
    } catch(const std::exception& e) {fprintf(stderr,"unresolved: %s\n",e.what());return 2;}
}
