// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <cerrno>
#include "xy_rust_seek.h"
#ifdef _WIN32
#include <windows.h>
#else
using std::min;using std::max;
typedef unsigned long long ULONGLONG;
typedef long long LONGLONG;
typedef unsigned int UINT;
#endif
struct SeekInterrupted {};
static std::vector<uint64_t> seekEvents;
static int seekFailure=-1,seekAttempts=0,seekMutation=0,seekMutations=0;
static bool seekPartial=false;
struct SeekBuffer {
    size_t& count;size_t& position;
    SeekBuffer(size_t& c,size_t& p):count(c),position(p){}
    size_t Unread()const{return count?count-position:0;}
    void Invalidate(){count=0;}
};
struct NativeSeekFile {
    enum {begin,current,end};
    int m_offset;
    size_t m_bufferCount,m_bufferPos;
    SeekBuffer m_rustBuffer;
    uint64_t nativePosition,nativeLength;
    NativeSeekFile():m_offset(0),m_bufferCount(0),m_bufferPos(0),m_rustBuffer(m_bufferCount,m_bufferPos),nativePosition(0),nativeLength(0){}
    void event(unsigned kind,uint64_t argument=0,unsigned origin=0){
        seekEvents.insert(seekEvents.end(),{kind,argument,origin,static_cast<uint64_t>(m_offset),m_bufferCount,m_bufferPos,nativePosition,nativeLength});
        bool fail=seekAttempts++==seekFailure;
        if(fail && !seekPartial)throw SeekInterrupted();
        if(seekMutation&(1<<kind)){m_offset=(m_offset+1)%4;m_bufferCount=7;m_bufferPos=3;nativePosition+=kind+1;nativeLength+=kind+1;++seekMutations;}
        if(fail)throw SeekInterrupted();
    }
    ULONGLONG GetPosition()const{auto self=const_cast<NativeSeekFile*>(this);self->event(0);return nativePosition;}
    ULONGLONG GetLength()const{auto self=const_cast<NativeSeekFile*>(this);self->event(1);return nativeLength;}
    ULONGLONG Seek(LONGLONG offset,UINT origin){event(2,static_cast<uint64_t>(offset),origin);nativePosition=static_cast<uint64_t>(offset)+(seekMutation&4?1:0);return nativePosition;}
    std::vector<uint64_t> snapshot()const{return {static_cast<uint64_t>(m_offset),m_bufferCount,m_bufferPos,nativePosition,nativeLength};}
};
static uint64_t seekHandled=0,seekDeclined=0;
namespace XyRust {
 template<class Offset> bool ObservedSeekTarget(uint64_t position,uint64_t length,Offset& offset,uint32_t origin,int bom){bool ok=SeekTarget(position,length,offset,origin,bom);if(ok)++seekHandled;else ++seekDeclined;return ok;}
}
