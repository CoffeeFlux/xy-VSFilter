// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "rle_storage_shell.h"
#include <deque>
#ifndef _WIN32
#define FALSE 0
typedef int32_t LONG;
typedef int32_t HRESULT;
static const HRESULT S_OK=0,S_FALSE=1,E_INVALIDARG=static_cast<HRESULT>(0x80070057u);
#endif
using std::max;
#define TRACE_DVB(x) ((void)0)
#define SAFE_DELETE(p) do{if(p){rleRelease(p);p=NULL;}}while(0)
static int dvbCopy(void* destination,size_t capacity,const void* source,size_t count){
    requireRle(count<=capacity);rleEvents.insert(rleEvents.end(),{10,static_cast<int64_t>(capacity),static_cast<int64_t>(count)});rleCopy(destination,source,count);return 0;
}
enum {ST_DVB=0};
struct CBaseSub {explicit CBaseSub(int type){requireRle(type==ST_DVB);}};
struct StoragePage {int id;explicit StoragePage(int value):id(value){}~StoragePage(){rleEvents.insert(rleEvents.end(),{11,id});}};
struct StoragePageOwner {StoragePage* value=NULL;~StoragePageOwner(){delete value;}void Free(){rleEvents.push_back(12);delete value;value=NULL;}};
struct StoragePageList {
    std::deque<StoragePage*> values;
    size_t GetCount()const{return values.size();}
    StoragePage* RemoveHead(){auto* p=values.front();values.pop_front();rleEvents.insert(rleEvents.end(),{13,p->id});return p;}
    ~StoragePageList(){requireRle(values.empty());}
};
#define DVB_STORAGE_CALLBACK_FIELDS typedef StoragePage DVB_PAGE;StoragePageList m_Pages;StoragePageOwner m_pCurrentPage;
