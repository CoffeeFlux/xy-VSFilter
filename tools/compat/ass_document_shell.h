// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Compose the already-tested dialogue/style sinks while preserving their own
// extracted-branch harnesses. The real style map and font registration remain
// integration boundaries; this sink records exact calls and retained state.
#define CSimpleTextSubtitle StyleSubtitle
#include "ass_style_shell.h"
#undef CSimpleTextSubtitle
#undef min
#undef max
#undef CRect
#define CSimpleTextSubtitle DialogueSubtitle
#define CTextFile DialogueFile
#include "ass_dialogue_shell.h"
#undef CSimpleTextSubtitle
#undef CTextFile
#undef min
#undef CRect
#include "xy_rust_ass_headers.h"
static uint64_t headerOwners=0,headerClassifications=0,headerValues=0,headerVersions=0;
namespace XyRust {
class ObservedAssHeaders : public AssHeaders {
public:
    ObservedAssHeaders(){++headerOwners;}
    template<class String> uint32_t Classify(const String& input){++headerClassifications;return AssHeaders::Classify(input);}
    template<class String> uint32_t Value(const String& input,uint32_t mode)const{++headerValues;return AssHeaders::Value(input,mode);}
    void ScriptVersion(int version){++headerVersions;AssHeaders::ScriptVersion(version);}
};
}
struct DocumentSize {
    int32_t cx,cy;
    DocumentSize(int x=0,int y=0):cx(x),cy(y){}
};
struct DocumentSubtitle : public StyleSubtitle,public DialogueSubtitle {
    enum Matrix {YCbCrMatrix_BT601,YCbCrMatrix_BT709,YCbCrMatrix_BT2020,YCbCrMatrix_AUTO};
    enum Range {YCbCrRange_PC,YCbCrRange_TV,YCbCrRange_AUTO};
    Matrix m_eYCbCrMatrix;Range m_eYCbCrRange;
    DocumentSize m_dstScreenSize,m_layout_size;
    int m_defaultWrapStyle,m_collisions;bool m_fScaledBAS;
    DocumentSubtitle():m_eYCbCrMatrix(YCbCrMatrix_AUTO),m_eYCbCrRange(YCbCrRange_PC),m_defaultWrapStyle(2),m_collisions(7),m_fScaledBAS(true){}
    void AddSTSEntryOnly(const MicroString& text,bool unicode,int64_t start,int64_t end,const MicroString& style,const MicroString& actor,const MicroString& effect,const StyleRect& margin,int layer){
        DialogueRect rect={margin.left,margin.top,margin.right,margin.bottom};
        DialogueSubtitle::AddSTSEntryOnly(text,unicode,start,end,style,actor,effect,rect,layer);
    }
    std::vector<int32_t> settings()const{return {m_eYCbCrMatrix,m_eYCbCrRange,m_dstScreenSize.cx,m_dstScreenSize.cy,m_layout_size.cx,m_layout_size.cy,m_defaultWrapStyle,m_collisions,m_fScaledBAS?1:0};}
};
struct DocumentLine {std::wstring text;bool success;};
static uint64_t documentReads=0,documentFonts=0,documentMutations=0;
static bool documentMutate=false;
static DocumentSubtitle* activeDocument=NULL;
struct DocumentFile : public DialogueFile {
    std::vector<DocumentLine> lines;size_t position;
    DocumentFile(const std::vector<DocumentLine>& input,bool isUnicode):lines(input),position(0){unicode=isUnicode;}
    bool ReadString(MicroString& text){
        bool success=false;
        if(position<lines.size()){
            const auto& line=lines[position++];text=MicroString(line.text.data(),static_cast<int>(line.text.size()));success=line.success;
        }else{text.Empty();}
        ++documentReads;microObserve(38,text.GetString());searchEvents.push_back(success?1:0);searchEvents.push_back(position);
        if(documentMutate && position==3){activeDocument->m_dstScreenSize.cy=777;activeDocument->m_collisions=-3;++documentMutations;}
        return success;
    }
};
// Same visible trimming rules at a modeled utility boundary: explicit-length
// right whitespace, then CString's native left trim. The utility itself is not
// migrated or claimed as directly extracted by this document harness.
static void FastTrim(MicroString& text){
    int length=text.GetLength();while(length>0 && std::iswspace(text[length-1]))--length;
    if(length!=text.GetLength())text.Truncate(length);text.TrimLeft();
}
static bool LoadFont(MicroString& font){
    ++documentFonts;microObserve(39,font.GetString());
    // Record the counted font data; OS font registration is outside this shell.
    searchEvents.push_back(font.GetLength());for(int i=0;i<font.GetLength();++i)searchEvents.push_back(static_cast<uint16_t>(font[i]));
    return true;
}
#define CSimpleTextSubtitle DocumentSubtitle
#define CTextFile DocumentFile
#define CSize DocumentSize
#define CRect StyleRect
#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))
