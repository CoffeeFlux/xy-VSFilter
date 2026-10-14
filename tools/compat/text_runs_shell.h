// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "microdvd_shell.h"
#include "xy_rust_text_runs.h"
#include <memory>
#include <new>
#include <cerrno>
struct FwSTSStyle {int id;};
struct RenderState {int m_ktype,m_kstart,m_kend;double m_target_scale_x,m_target_scale_y;};
static uint64_t runOwners=0,runSteps=0,runPrepared=0,runNativePrepared=0,runResets=0;
namespace XyRust {
class ObservedTextRuns : public TextRuns {
public:
    template<class String> explicit ObservedTextRuns(const String& input):TextRuns(input){++runOwners;}
    bool Prepare(int wrap){bool handled=TextRuns::Prepare(wrap);if(handled)++runPrepared;else ++runNativePrepared;return handled;}
    template<class String> void Reset(const String& input){++runResets;TextRuns::Reset(input);}
    uint32_t Next(){++runSteps;return TextRuns::Next();}
};
}
static std::vector<uint64_t> runRecord(const MicroString& text,const FwSTSStyle& style,int type,int start,int end,double sx,double sy){
    std::vector<uint64_t> result={static_cast<uint32_t>(style.id),static_cast<uint32_t>(type),static_cast<uint32_t>(start),static_cast<uint32_t>(end)};
    uint64_t x,y;std::memcpy(&x,&sx,8);std::memcpy(&y,&sy,8);result.push_back(x);result.push_back(y);result.push_back(text.GetLength());
    for(int i=0;i<text.GetLength();++i)result.push_back(static_cast<uint16_t>(text[i]));return result;
}
static int liveRunAllocations=0;
struct CWord {std::vector<uint64_t> record;virtual ~CWord(){};};
typedef CWord* PCWord;
typedef std::shared_ptr<CWord> SharedPtrCWord;
struct CText : CWord {
    CText(const FwSTSStyle& style,const MicroString& text,int type,int start,int end,double sx,double sy){
        auto value=runRecord(text,style,type,start,end,sx,sy);
        microObserve(59,text.GetString());searchEvents.insert(searchEvents.end(),value.begin(),value.end());record=value;
    }
    static void* operator new(size_t size){void* result=::operator new(size);++liveRunAllocations;return result;}
    static void operator delete(void* pointer){--liveRunAllocations;::operator delete(pointer);}
};
struct RunWords {
    std::vector<SharedPtrCWord> values;
    void AddTail(const SharedPtrCWord& word){microObserve(60,L"");searchEvents.insert(searchEvents.end(),word->record.begin(),word->record.end());values.push_back(word);}
    std::vector<std::vector<uint64_t>> snapshot()const{std::vector<std::vector<uint64_t>> out;for(const auto& word:values)out.push_back(word->record);return out;}
};
struct CSubtitle {int m_wrapStyle;RunWords m_words;};
#define DEBUG_NEW new
