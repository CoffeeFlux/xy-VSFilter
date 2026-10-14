// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rle_storage_shell.h"
#include "vobsub_trim_shell.h"
#ifndef FALSE
#define FALSE 0
#endif
#define SAFE_DELETE_ARRAY(p) do{if(p){rleRelease(reinterpret_cast<BYTE*>(p));p=nullptr;}}while(0)
static int vobFailure=-1,vobAttempt=0;
static uint64_t vobMemoryFailures=0,vobExceptionDeletes=0;
struct CMemoryException {void Delete(){++vobExceptionDeletes;rleEvents.push_back(14);}};
static CMemoryException vobMemoryError;
static RGBQUAD* vobAllocate(int count){
    requireRle(count>=0);const size_t bytes=static_cast<size_t>(count)*sizeof(RGBQUAD);
    if(vobAttempt++==vobFailure){++vobMemoryFailures;rleEvents.insert(rleEvents.end(),{1,static_cast<int64_t>(bytes)});throw &vobMemoryError;}
    return reinterpret_cast<RGBQUAD*>(rleAllocate(bytes));
}
namespace XyRust {
class ObservedVobStorage:public ByteStorage {
public:
    bool Allocate(size_t count){
        const bool previous=rleDecline;if(vobAttempt==vobFailure)rleDecline=true;
        const bool result=ByteStorage::Allocate(count);rleDecline=previous;
        if(result)++vobAttempt;return result;
    }
};
}
#define VOB_STORAGE_EXTRA_FIELDS \
    size_t nOffset[2],nPlane;bool fCustomPal,fAligned;int tridx;RGBQUAD* orgpal;RGBQUAD* cuspal; \
    size_t iLang,iIdx;bool fForced,fAnimated;int tCurrent;int64_t start,delay;CRect rect; \
    struct SubPal{BYTE pal:4,tr:4;};SubPal pal[4];RGBQUAD* lpPixels;
