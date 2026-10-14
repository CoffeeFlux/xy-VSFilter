// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "microdvd_shell.h"
#include "xy_rust_fields.h"
#include "xy_rust_ass_dialogue.h"
#include <cerrno>
#include <limits>
struct DialogueRect { int32_t left,top,right,bottom; };
struct CTextFile {
    bool unicode;
    bool IsUnicode()const{searchObserve(34,unicode?1:0);return unicode;}
};
struct DialogueEntry {
    std::vector<uint16_t> text,style,actor,effect;
    bool unicode;int64_t start,end;int layer;DialogueRect margin;
    bool operator==(const DialogueEntry& b)const{
        return text==b.text && style==b.style && actor==b.actor && effect==b.effect &&
            unicode==b.unicode && start==b.start && end==b.end && layer==b.layer &&
            margin.left==b.margin.left && margin.right==b.margin.right && margin.top==b.margin.top && margin.bottom==b.margin.bottom;
    }
};
static std::vector<uint16_t> dialogueUnits(const MicroString& str){
    std::vector<uint16_t> result;for(int i=0;i<str.GetLength();++i)result.push_back(static_cast<uint16_t>(str[i]));return result;
}
struct CSimpleTextSubtitle {
    std::vector<DialogueEntry> entries;
    void AddSTSEntryOnly(const MicroString& text,bool unicode,int64_t start,int64_t end,const MicroString& style,const MicroString& actor,const MicroString& effect,const DialogueRect& margin,int layer){
        searchObserve(35,0);
        DialogueEntry entry={dialogueUnits(text),dialogueUnits(style),dialogueUnits(actor),dialogueUnits(effect),unicode,start,end,layer,margin};
        entries.push_back(entry);
    }
};
static uint64_t dialogueOwners=0,dialogueSteps=0,numericConversions=0;
namespace XyRust {
class ObservedAssDialogue : public AssDialogue {
public:
    template<class String> ObservedAssDialogue(const String& input,int version):AssDialogue(input,version){++dialogueOwners;}
    uint32_t Step(int reply){++dialogueSteps;return AssDialogue::Step(reply);}
};
}
static long dialogueSigned(const wchar_t* text,wchar_t** end,int base){
    ++numericConversions;microObserve(32,text);searchEvents.push_back(base);
    const long result=std::wcstol(text,end,base);
    searchEvents.push_back(static_cast<uint64_t>(result));searchEvents.push_back(*end-text);searchEvents.push_back(errno);
    return result;
}
static unsigned long dialogueUnsigned(const wchar_t* text,wchar_t** end,int base){
    ++numericConversions;microObserve(33,text);searchEvents.push_back(base);
    const unsigned long result=std::wcstoul(text,end,base);
    searchEvents.push_back(static_cast<uint64_t>(result));searchEvents.push_back(*end-text);searchEvents.push_back(errno);
    return result;
}
#define wcstol dialogueSigned
#define wcstoul dialogueUnsigned
#define CString MicroString
#define CRect DialogueRect
#define MS2RT(t) (10000LL * (t))
#ifndef _T
#define _T(text) L##text
#endif
#define min std::min
