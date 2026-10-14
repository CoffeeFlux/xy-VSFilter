// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sami_shell.h"
#include "xy_rust_sami_file.h"
#include <limits>
#ifndef _WIN32
typedef uint64_t ULONGLONG;
#endif
struct CFile { enum {begin=0}; };
#define MS2RT(value) (static_cast<int64_t>(value)*10000)
#define LONGLONG_MAX (std::numeric_limits<int64_t>::max())
static uint64_t fileOwners=0,fileBegins=0,fileAppends=0,fileFlushes=0,fileEnds=0;
namespace XyRust {
class ObservedSamiFile : public SamiFile {
public:
    ObservedSamiFile(){++fileOwners;}
    template<class String> uint32_t Begin(const String& line){++fileBegins;return SamiFile::Begin(line);}
    template<class String> void Append(const String& line){++fileAppends;SamiFile::Append(line);}
    void Flushed(int time){++fileFlushes;SamiFile::Flushed(time);}
    void End(){++fileEnds;SamiFile::End();}
};
}
static void fileText(const MicroString& text){
    searchEvents.push_back(text.GetLength());
    for(int i=0;i<text.GetLength();++i)searchEvents.push_back(static_cast<uint16_t>(text[i]));
}
struct SamiLine {std::wstring text;bool success;};
struct CTextFile {
    std::vector<SamiLine> lines;size_t position;bool unicode;
    CTextFile(const std::vector<SamiLine>& input,bool wide,size_t pos):lines(input),position(pos),unicode(wide){}
    ULONGLONG GetPosition(){microObserve(45,L"");searchEvents.push_back(position);return position;}
    void Seek(ULONGLONG pos,int origin){microObserve(46,L"");searchEvents.push_back(pos);searchEvents.push_back(origin);position=static_cast<size_t>(pos);}
    bool ReadString(MicroString& text){
        bool success=false;
        if(position<lines.size()){
            const auto& line=lines[position++];text=MicroString(line.text.data(),static_cast<int>(line.text.size()));success=line.success;
        }else{text.Empty();}
        microObserve(47,text.GetString());fileText(text);searchEvents.push_back(position);searchEvents.push_back(success?1:0);return success;
    }
    bool IsUnicode(){microObserve(48,L"");searchEvents.push_back(unicode?1:0);return unicode;}
};
struct SamiEntry {
    std::vector<uint16_t> text;bool unicode;int64_t start,end;
    bool operator==(const SamiEntry& rhs)const{return text==rhs.text && unicode==rhs.unicode && start==rhs.start && end==rhs.end;}
};
struct CSimpleTextSubtitle {
    std::vector<SamiEntry> entries;
    void Add(const MicroString& text,bool unicode,int64_t start,int64_t end){
        microObserve(49,text.GetString());fileText(text);searchEvents.push_back(unicode?1:0);
        searchEvents.push_back(static_cast<uint64_t>(start));searchEvents.push_back(static_cast<uint64_t>(end));
        SamiEntry entry;entry.unicode=unicode;entry.start=start;entry.end=end;
        for(int i=0;i<text.GetLength();++i)entry.text.push_back(static_cast<uint16_t>(text[i]));
        entries.push_back(entry);
    }
};
// Controlled file/sink and trimming boundaries; Windows CString supplies its
// actual case and left-trim behavior. The utility itself is not migrated here.
static void FastTrim(MicroString& text){
    int length=text.GetLength();while(length>0 && std::iswspace(text[length-1]))--length;
    if(length!=text.GetLength())text.Truncate(length);text.TrimLeft();
}
#define iswdigit observedDigit
