// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#include <windows.h>
#else
typedef char16_t WCHAR;
#define CP_UTF8 65001
#define DEFAULT_CHARSET 1
#endif
#define CSRIAPI
#define TEXT(x) x
#define AMTRACE(x) ((void)0)
typedef const char* csri_rend;
struct csri_openflag {};
static void filenameRequire(bool ok){if(!ok){fprintf(stderr,"CSRI filename fixture outside scope\n");std::abort();}}
struct FilenameRegion {WCHAR* data;uint8_t* raw;void* owner;size_t count,written;};
static std::vector<FilenameRegion> filenameRegions;
static std::vector<uint64_t> filenameEvents;
static std::vector<uint16_t> filenameReply;
static bool filenameDecline=false,filenameOpen=true,filenameConversionFailure=false;
static unsigned filenameConversions=0,filenameInstances=0,filenameLocks=0,filenameRenderers=0;
static uint64_t filenameOwners=0,filenameLoans=0,filenameReleases=0,filenameDeclines=0,filenameBytes=0,filenameUnwritten=0;
static FilenameRegion& filenameRegion(const void* p){for(auto& r:filenameRegions)if(r.data==p)return r;filenameRequire(false);return filenameRegions[0];}
static void filenameSnapshot(const FilenameRegion& r){
    filenameEvents.insert(filenameEvents.end(),{r.count,r.written});
    const auto* p=reinterpret_cast<const uint8_t*>(r.data);
    for(size_t i=0;i<r.written;++i){filenameEvents.push_back(p[i]);++filenameBytes;}
    filenameUnwritten+=r.count-r.written;
    if(r.raw)for(size_t i=0;i<16;++i)filenameRequire(r.raw[i]==0xbd&&p[r.count+i]==0xbd);
}
static WCHAR* filenameNew(size_t count){auto* raw=new uint8_t[count*sizeof(WCHAR)+32];std::memset(raw,0xbd,count*sizeof(WCHAR)+32);auto* p=reinterpret_cast<WCHAR*>(raw+16);filenameRegions.push_back({p,raw,NULL,count*sizeof(WCHAR),0});filenameEvents.insert(filenameEvents.end(),{1,count*sizeof(WCHAR)});return p;}
static void filenameDelete(WCHAR* p){for(size_t i=0;i<filenameRegions.size();++i)if(filenameRegions[i].data==p){auto r=filenameRegions[i];filenameRequire(r.raw&&!r.owner);filenameEvents.push_back(2);filenameSnapshot(r);delete[] r.raw;filenameRegions.erase(filenameRegions.begin()+i);return;}filenameRequire(false);}
#include "xy_rust_plane.h"
static void* filenameCreate(size_t count){if(filenameDecline){++filenameDeclines;return NULL;}void* owner=xy_vsfilter_plane_create(count);filenameRequire(owner!=NULL);filenameRegions.push_back({NULL,NULL,owner,count,0});++filenameOwners;return owner;}
static uint8_t* filenameLoan(void* owner){auto* data=xy_vsfilter_plane_loan(owner);for(auto& r:filenameRegions)if(r.owner==owner){filenameRequire(!r.data);r.data=reinterpret_cast<WCHAR*>(data);filenameEvents.insert(filenameEvents.end(),{1,r.count});++filenameLoans;return data;}filenameRequire(false);return NULL;}
static void filenameRelease(void* owner){if(!owner){xy_vsfilter_plane_release(owner);return;}for(size_t i=0;i<filenameRegions.size();++i)if(filenameRegions[i].owner==owner){filenameEvents.push_back(2);filenameSnapshot(filenameRegions[i]);filenameRegions.erase(filenameRegions.begin()+i);xy_vsfilter_plane_release(owner);++filenameReleases;return;}filenameRequire(false);}
#define xy_vsfilter_plane_create filenameCreate
#define xy_vsfilter_plane_loan filenameLoan
#define xy_vsfilter_plane_release filenameRelease
#include "xy_rust_byte_storage.h"
#undef xy_vsfilter_plane_create
#undef xy_vsfilter_plane_loan
#undef xy_vsfilter_plane_release
static int filenameConvert(unsigned codepage,unsigned flags,const char* source,int length,WCHAR* destination,int capacity){
    filenameRequire(codepage==CP_UTF8&&flags==0&&length==-1&&source);
    ++filenameConversions;filenameEvents.insert(filenameEvents.end(),{3,codepage,flags,static_cast<uint64_t>(length),destination?1U:0U,static_cast<uint64_t>(capacity)});
    if(filenameConversionFailure){filenameRequire(!destination&&!capacity);return 0;}
    int result=0;
#ifdef _WIN32
    result=::MultiByteToWideChar(codepage,flags,source,length,destination,capacity);
#else
    result=static_cast<int>(filenameReply.size());
    if(destination){filenameRequire(capacity>=result);for(size_t i=0;i<filenameReply.size();++i)destination[i]=filenameReply[i];}
#endif
    filenameEvents.push_back(result);
    if(destination){auto& r=filenameRegion(destination);filenameRequire(result>0&&static_cast<size_t>(result)*sizeof(WCHAR)<=r.count&&destination[result-1]==0);r.written=static_cast<size_t>(result)*sizeof(WCHAR);}
    return result;
}
class CString {public:std::vector<uint16_t> units;explicit CString(const WCHAR* p){const auto& r=filenameRegion(p);size_t n=0;while(n*sizeof(WCHAR)<r.written&&p[n]){units.push_back(static_cast<uint16_t>(p[n++]));}filenameRequire(n*sizeof(WCHAR)<r.written);filenameEvents.push_back(4);filenameEvents.insert(filenameEvents.end(),units.begin(),units.end());filenameEvents.push_back(0);}};
struct CCritSec {CCritSec(){++filenameLocks;filenameEvents.push_back(5);}~CCritSec(){--filenameLocks;filenameEvents.push_back(6);}};
struct CRenderedTextSubtitle {explicit CRenderedTextSubtitle(CCritSec* cs){filenameRequire(cs&&filenameLocks==1);++filenameRenderers;filenameEvents.push_back(7);}bool Open(const CString&,int charset){filenameRequire(charset==DEFAULT_CHARSET);filenameEvents.insert(filenameEvents.end(),{8,static_cast<unsigned>(charset),filenameOpen?1U:0U});return filenameOpen;}~CRenderedTextSubtitle(){--filenameRenderers;filenameEvents.push_back(9);}};
struct csri_inst {CRenderedTextSubtitle* rts;CCritSec* cs;size_t readorder;csri_inst():rts(NULL),cs(NULL),readorder(0){++filenameInstances;filenameEvents.push_back(10);}~csri_inst(){--filenameInstances;filenameEvents.push_back(11);}};
