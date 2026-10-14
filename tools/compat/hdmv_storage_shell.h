// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rle_storage_shell.h"
#include "xy_rust_golomb.h"
#ifdef _WIN32
#include <vfwmsgs.h>
#else
typedef uint64_t UINT64;
typedef int64_t INT64;
typedef int32_t HRESULT;
static const HRESULT S_OK=0;
static const HRESULT VFW_E_SAMPLE_REJECTED=static_cast<HRESULT>(0x8004022bu);
#endif
typedef int64_t REFERENCE_TIME;
using std::min;
#define TRACE_HDMVSUB(x) ((void)0)
#include "segment_golomb.h"
enum { ST_HDMV=1 };
struct CBaseSub {explicit CBaseSub(int type){requireRle(type==ST_HDMV);}};
struct DummyPresentation {~DummyPresentation(){rleEvents.push_back(9);}};
static uint64_t segmentDispatches=0,partialDispatches=0;
template<class Reader>class TrackedSegmentReader:public Reader {
public:
    TrackedSegmentReader(BYTE* p,int count):Reader(p,count){}
    void ReadBuffer(BYTE* destination,int count){
        requireRle(count>=0&&this->m_nBitPos>=0&&this->m_nBitPos<=this->m_nSize);
        rleEvents.insert(rleEvents.end(),{5,count,this->m_nBitPos,this->m_nSize});
        Reader::ReadBuffer(destination,count);
    }
};
template<class Reader>static void segmentDispatch(int operation,Reader* reader,int size,REFERENCE_TIME time){
    ++segmentDispatches;rleEvents.insert(rleEvents.end(),{6,operation,size,time,reader->m_nBitPos,reader->m_bitlen,reader->m_bitbuff});
    if(!size)return;
    size_t offset;const auto& region=rleRegion(reader->m_pBuffer,size,offset);bool partial=false;
    for(int i=0;i<size;++i){const size_t index=offset+i;rleEvents.push_back(region.initialized[index]);if(region.initialized[index])rleEvents.push_back(region.data[index]);else partial=true;}
    partialDispatches+=partial;
}
#define SEGMENT_CALLBACKS \
    void Reset(){rleEvents.push_back(7);} \
    void ParsePalette(CGolombBuffer* reader,unsigned short count){segmentDispatch(1,reader,count,0);} \
    void ParseObject(CGolombBuffer* reader,unsigned short count){segmentDispatch(2,reader,count,0);} \
    int ParsePresentationSegment(REFERENCE_TIME time,CGolombBuffer* reader){segmentDispatch(3,reader,reader->GetSize(),time);return 0;} \
    void EnqueuePresentationSegment(REFERENCE_TIME time){rleEvents.insert(rleEvents.end(),{8,time});}
