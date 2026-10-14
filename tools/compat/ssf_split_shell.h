// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <memory>
#include <cwctype>
#include <cwchar>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#include <windows.h>
#include <atlbase.h>
#include <atlcoll.h>
#include <atlstr.h>
#else
typedef char16_t WCHAR;
typedef const WCHAR* LPCWSTR;
class CStringW {
    std::u16string value;
public:
    CStringW(){}
    CStringW(LPCWSTR input):value(input){}
    CStringW(WCHAR unit):value(1,unit){}
    CStringW(LPCWSTR input,int length):value(input,input+length){}
    int GetLength()const{return static_cast<int>(value.size());}
    bool IsEmpty()const{return value.empty();}
    LPCWSTR GetString()const{return value.c_str();}
    operator LPCWSTR()const{return GetString();}
    WCHAR operator[](int index)const{return value.at(index);}
    int Find(LPCWSTR separator,int start)const{
        if(start<0||static_cast<size_t>(start)>value.size())return -1;
        const std::u16string visible(value.c_str()+start);
        const size_t found=visible.find(separator);
        return found==std::u16string::npos?-1:start+static_cast<int>(found);
    }
    CStringW Mid(int first,int count)const{CStringW result;result.value=value.substr(first,count);return result;}
    void Trim(){
        // Portable string/locale boundary. Windows runs actual CStringW Trim.
        size_t start=0,end=value.size();
        while(start<end&&std::iswspace(value[start]))++start;
        while(end>start&&std::iswspace(value[end-1]))--end;
        value=value.substr(start,end-start);
    }
};
template<class T>class CAtlArray {
    std::vector<T> values;
public:
    size_t GetCount()const{return values.size();}
    const T& GetAt(size_t index)const{return values.at(index);}
    void RemoveAll(){values.clear();}
    size_t Add(const T& value){values.push_back(value);return values.size()-1;}
};
#endif
static const WCHAR splitEmpty[]={0};
static size_t ssfSplitLength(LPCWSTR value){
#ifdef _WIN32
    return wcslen(value);
#else
    return std::char_traits<WCHAR>::length(value);
#endif
}
struct SplitInterrupted {};
struct SplitContext {int fail=-1,attempt=0;std::vector<int64_t> events;};
static SplitContext* splitContext=NULL;
template<class T>class SsfSplitArray:public CAtlArray<T> {
public:
    void RemoveAll(){splitContext->events.insert(splitContext->events.end(),{0,static_cast<int64_t>(this->GetCount())});CAtlArray<T>::RemoveAll();}
    size_t Add(const T& value){
        splitContext->events.insert(splitContext->events.end(),{1,splitContext->attempt,value.GetLength()});
        for(int i=0;i<value.GetLength();++i)splitContext->events.push_back(value[i]);
        if(splitContext->attempt++==splitContext->fail)throw SplitInterrupted();
        return CAtlArray<T>::Add(value);
    }
};
#include "xy_rust_ssf_split.h"
static uint64_t ssfSplitSearches=0;
static int32_t ssfSplitFind(const uint16_t* input,int32_t length,const uint16_t* separator,size_t separatorLength,int32_t start){++ssfSplitSearches;return xy_vsfilter_ssf_split_find(input,length,separator,separatorLength,start);}
