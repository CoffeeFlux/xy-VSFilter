// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <list>
#include "xy_rust_realtext.h"
static std::vector<int64_t> realTextConversions;
static std::wstring* realTextTimecodeInput=nullptr;
static int realTextTimecodeFailure=-1,realTextTimecodeAttempts=0,realTextTimecodeMutation=0;
static bool realTextTimecodeMutated=false;
struct RealTextInterrupted {};
static std::wstring* realTextInput=nullptr;
static unsigned* realTextPosition=nullptr;
static std::map<std::wstring,std::wstring>* realTextAttributes=nullptr;
static std::vector<int64_t> realTextAttributeEvents;
static int realTextFailure=-1,realTextAttempts=0,realTextMutation=0;
static bool realTextMutated=false;
static void (*realTextTagHook)(int,const std::wstring&)=nullptr;
static void realTextStringEvent(const std::wstring& text){realTextAttributeEvents.push_back(text.size());for(wchar_t c:text)realTextAttributeEvents.push_back(c);}
static void realTextAttributeHook(int action,const std::wstring& name){
    if(realTextTagHook)realTextTagHook(action,name);
    if(!realTextInput)return;
    realTextAttributeEvents.insert(realTextAttributeEvents.end(),{action,*realTextPosition});realTextStringEvent(name);realTextStringEvent(*realTextInput);
    realTextAttributeEvents.push_back(realTextAttributes->size());for(const auto& item:*realTextAttributes){realTextStringEvent(item.first);realTextStringEvent(item.second);}
    if(realTextAttempts++==realTextFailure)throw RealTextInterrupted();
    if(!realTextMutated && realTextMutation==1 && action==2){realTextInput->replace(*realTextPosition,std::wstring::npos,L" b='changed'/>");realTextMutated=true;}
    if(!realTextMutated && realTextMutation==2 && action==1){realTextInput->clear();realTextMutated=true;}
}
static int realTextSpace(wint_t c){const int result=::iswspace(c);realTextConversions.insert(realTextConversions.end(),{3,int64_t(c),result});realTextAttributeHook(1,std::wstring(1,wchar_t(c)));return result;}
static int realTextDigit(wint_t c){const int result=::iswdigit(c);realTextConversions.insert(realTextConversions.end(),{1,int64_t(c),result});return result;}
static int realTextToInt(const wchar_t* text){
#ifdef _WIN32
    const int result=::_wtoi(text);
#else
    const int result=static_cast<int>(std::wcstol(text,nullptr,10));
#endif
    realTextConversions.push_back(2);realTextConversions.push_back(result);
    while(*text)realTextConversions.push_back(*text++);realTextConversions.push_back(0);
    if(realTextTimecodeInput){
        if(realTextTimecodeAttempts++==realTextTimecodeFailure)throw RealTextInterrupted();
        if(!realTextTimecodeMutated && realTextTimecodeMutation){
            if(realTextTimecodeMutation==1 && !realTextTimecodeInput->empty())(*realTextTimecodeInput)[0]=L'3';
            if(realTextTimecodeMutation==2)realTextTimecodeInput->clear();
            if(realTextTimecodeMutation==3)*realTextTimecodeInput+=L":999";
            realTextTimecodeMutated=true;
        }
    }
    return result;
}
#define iswdigit realTextDigit
#define iswspace realTextSpace
#define _wtoi realTextToInt
