// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <deque>
#include <string>
#include <cwctype>
#include <cwchar>
#include <cerrno>
#include <cfenv>
#ifdef _WIN32
#include <windows.h>
#include <atlbase.h>
#include <atlcoll.h>
#include <tchar.h>
#else
typedef char16_t WCHAR;
typedef const wchar_t* LPCWSTR;
typedef const wchar_t* LPCTSTR;
#define _T(x) L##x
#endif
static void ssfRequire(bool ok){if(!ok){fprintf(stderr,"SSF stream fixture outside scope\n");std::abort();}}
#ifndef _WIN32
template<class T>class CAtlList {std::deque<T> values;public:size_t GetCount()const{return values.size();}bool IsEmpty()const{return values.empty();}const T& GetHead()const{ssfRequire(!values.empty());return values.front();}const T& GetTail()const{ssfRequire(!values.empty());return values.back();}void AddTail(T v){values.push_back(v);}T RemoveHead(){ssfRequire(!values.empty());T v=values.front();values.pop_front();return v;}};
#endif
#ifdef ASSERT
#undef ASSERT
#endif
#define ASSERT(x) ssfRequire(x)
struct SsfInterrupted {int kind;};
struct SsfContext {std::vector<int> input;size_t position=0;int attempts=0,fail=-1;std::vector<int64_t> events;};
static SsfContext* ssfContext=NULL;
static int ssfNextChar(){auto& c=*ssfContext;c.events.insert(c.events.end(),{1,c.attempts,static_cast<int64_t>(c.position)});if(c.attempts++==c.fail)throw SsfInterrupted{2};int value=c.position<c.input.size()?c.input[c.position++]:-1;c.events.push_back(value);return value;}
static void ssfThrow(int line,int column,const wchar_t* message){ssfContext->events.insert(ssfContext->events.end(),{2,line,column});for(const wchar_t* p=message;*p;++p)ssfContext->events.push_back(*p);throw SsfInterrupted{1};}

#include "xy_rust_ssf_stream.h"
static uint64_t ssfOwners=0,ssfReleases=0,ssfPushes=0,ssfPops=0;
namespace XyRust {
class ObservedSsfLookahead:public SsfLookahead {public:ObservedSsfLookahead(){++ssfOwners;}~ObservedSsfLookahead(){++ssfReleases;}void AddTail(int value){++ssfPushes;SsfLookahead::AddTail(value);}int RemoveHead(){++ssfPops;return SsfLookahead::RemoveHead();}};
}
