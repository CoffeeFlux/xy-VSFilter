// SPDX-License-Identifier: GPL-2.0-or-later
// Full native class lifecycle probe: compile this source separately against the
// pinned and candidate checkouts' actual static libraries. No copied methods,
// reference member invalidation, allocator failures, dialogs or rendering.
#include <stdafx.h> // runner puts the selected checkout's src/subtitles first
#include <STS.h>
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
#include <RustDocumentOwner.h>
#endif
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <vector>
#include <utility>

static unsigned sequence = 0, empty_events[2] = {}, change_events[2] = {};
static void check(bool value, const char* message) {
    if(!value) throw std::runtime_error(message);
}
template<class String> static void units(const String& value) {
    printf(" %d",value.GetLength());
    for(int i=0;i<value.GetLength();i++) printf(" %04x",unsigned(static_cast<uint16_t>(value[i])));
}
static void real(double value) {
    static_assert(sizeof(double)==sizeof(uint64_t),"IEEE double trace width");
    uint64_t bits;std::memcpy(&bits,&value,sizeof bits);printf(" %016llx",static_cast<unsigned long long>(bits));
}
static void style(const STSStyle& value) {
    printf("style %d",value.charSet);units(value.fontName);real(value.fontSize);
    printf(" %d %d %d %d",value.fontWeight,value.fItalic,value.fUnderline,value.fStrikeOut);
    const RECT margin=value.marginRect.get();
    printf(" %ld %ld %ld %ld %d %d",margin.left,margin.top,margin.right,margin.bottom,value.scrAlignment,value.borderStyle);
    real(value.outlineWidthX);real(value.outlineWidthY);real(value.shadowDepthX);real(value.shadowDepthY);
    for(int i=0;i<4;i++)printf(" %08lx %u",static_cast<unsigned long>(value.colors[i]),unsigned(value.alpha[i]));
    real(value.fontScaleX);real(value.fontScaleY);real(value.fontSpacing);real(value.fBlur);real(value.fGaussianBlur);
    real(value.fontAngleZ);real(value.fontAngleX);real(value.fontAngleY);real(value.fontShiftX);real(value.fontShiftY);
    printf(" %d\n",value.relativeTo);
}

#ifndef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
// The pinned class already grants this friend; neither checkout defines it.
// A probe-local accessor invokes the real private method, including append and
// self-clear behavior, without rewriting it or changing access specifications.
class CSubtitleEditorDlg {
public:
    static bool CopyNames(CSimpleTextSubtitle& destination,
                          const CSimpleTextSubtitle& source, bool append) {
        return destination.CopyStyles(source.m_styles,append);
    }
};
#endif

struct MetadataValue {
    bool forced;
    CSimpleTextSubtitle::EPARCompensationType parType;
    double par;
    CSimpleTextSubtitle::YCbCrMatrix matrix;
    CSimpleTextSubtitle::YCbCrRange range;
};
class TraceDocument : public CSimpleTextSubtitle {
    unsigned id_;
    MetadataValue initial_metadata_{},last_callback_metadata_{};
    CSize initial_screen_,last_callback_screen_,last_callback_layout_size_;int initial_wrap_,initial_collisions_;bool initial_scaled_;
    int initial_mode_;exttype initial_type_;CTextFile::enc initial_encoding_;
public:
    explicit TraceDocument(unsigned id) : id_(id) {
        initial_metadata_=Metadata();
        initial_screen_=Screen();initial_wrap_=Wrap();initial_collisions_=Collisions();initial_scaled_=Scaled();
        initial_mode_=Mode();initial_type_=Type();initial_encoding_=Encoding();
        // Assign every observed metadata scalar before the first observation;
        // constructor-indeterminate m_simple/m_lcid are never read.
        m_simple = id != 0;Name(id ? CString(L"destination") : CString(L"source"));
        LCIDValue(1033+id);Type(EXTSRT);Mode(TIME);Encoding(CTextFile::UTF8);
        Path(id ? CString(L"destination-path") : CString(L"source-path"));
        Screen(CSize(640+id,480+id));Wrap(int(id));Collisions(int(id));
        Scaled(id!=0);LayoutSize(CSize(800+id,600+id));
        STSStyle initialDefault;initialDefault.charSet=ANSI_CHARSET;initialDefault.fontSize=19+id;
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        m_document->SetDefaultStyle(initialDefault);
#else
        m_defaultStyle=initialDefault;
#endif
        Forced(false);PARType(EPCTDownscale);PAR(1.25+id);Matrix(YCbCrMatrix_BT709);Range(YCbCrRange_PC);
    }
    ~TraceDocument() override {printf("event %u %u destructor-body\n",++sequence,id_);}
    CSize LayoutSize() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentLayoutSize();
#else
        return m_layout_size;
#endif
    }
    void LayoutSize(const CSize& value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentLayoutSize(value);
#else
        m_layout_size=value;
#endif
    }
    CSize LastCallbackLayoutSize() const {return last_callback_layout_size_;}
    LCID LCIDValue() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentLCID();
#else
        return m_lcid;
#endif
    }
    void LCIDValue(LCID value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentLCID(value);
#else
        m_lcid=value;
#endif
    }
    bool Forced() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentForcedDefaultStyle();
#else
        return m_fForcedDefaultStyle;
#endif
    }
    void Forced(bool value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentForcedDefaultStyle(value);
#else
        m_fForcedDefaultStyle=value;
#endif
    }
    EPARCompensationType PARType() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentPARType();
#else
        return m_ePARCompensationType;
#endif
    }
    void PARType(EPARCompensationType value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentPARType(value);
#else
        m_ePARCompensationType=value;
#endif
    }
    double PAR() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentPARCompensation();
#else
        return m_dPARCompensation;
#endif
    }
    void PAR(double value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentPARCompensation(value);
#else
        m_dPARCompensation=value;
#endif
    }
    YCbCrMatrix Matrix() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentColorMatrix();
#else
        return m_eYCbCrMatrix;
#endif
    }
    void Matrix(YCbCrMatrix value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentColorMatrix(value);
#else
        m_eYCbCrMatrix=value;
#endif
    }
    YCbCrRange Range() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentColorRange();
#else
        return m_eYCbCrRange;
#endif
    }
    void Range(YCbCrRange value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentColorRange(value);
#else
        m_eYCbCrRange=value;
#endif
    }
    MetadataValue Metadata() const {return {Forced(),PARType(),PAR(),Matrix(),Range()};}
    MetadataValue InitialMetadata() const {return initial_metadata_;}
    MetadataValue CallbackMetadata() const {return last_callback_metadata_;}
    CSize Screen() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentScreenSize();
#else
        return m_dstScreenSize;
#endif
    }
    void Screen(const CSize& value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentScreenSize(value);
#else
        m_dstScreenSize=value;
#endif
    }
    int Wrap() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentWrapStyle();
#else
        return m_defaultWrapStyle;
#endif
    }
    void Wrap(int value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentWrapStyle(value);
#else
        m_defaultWrapStyle=value;
#endif
    }
    int Collisions() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentCollisions();
#else
        return m_collisions;
#endif
    }
    void Collisions(int value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentCollisions(value);
#else
        m_collisions=value;
#endif
    }
    bool Scaled() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentScaledBAS();
#else
        return m_fScaledBAS;
#endif
    }
    void Scaled(bool value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentScaledBAS(value);
#else
        m_fScaledBAS=value;
#endif
    }
    CSize LastCallbackScreen() const {return last_callback_screen_;}
    void ConstructionLayout() const {
        check(initial_screen_==CSize(0,0)&&initial_wrap_==0&&initial_collisions_==0&&!initial_scaled_,"native initialized layout defaults");
        printf("layout-stage layout-constructed width=%ld height=%ld wrap=%d collisions=%d scaled=%d\n",initial_screen_.cx,initial_screen_.cy,initial_wrap_,initial_collisions_,initial_scaled_);
    }
    int Mode() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentMode();
#else
        return m_mode;
#endif
    }
    void Mode(int value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentMode(static_cast<tmode>(value));
#else
        m_mode=static_cast<tmode>(value);
#endif
    }
    exttype Type() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentType();
#else
        return m_subtitleType;
#endif
    }
    void Type(exttype value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentType(value);
#else
        m_subtitleType=value;
#endif
    }
    CTextFile::enc Encoding() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentEncoding();
#else
        return m_encoding;
#endif
    }
    void Encoding(CTextFile::enc value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentEncoding(value);
#else
        m_encoding=value;
#endif
    }
    void ConstructionInterpretation() const {
        check(initial_mode_==TIME&&initial_type_==EXTSRT&&initial_encoding_==CTextFile::DEFAULT_ENCODING,"native initialized interpretation defaults");
        printf("interpretation-stage interpretation-constructed mode=%d type=%d encoding=%d\n",initial_mode_,int(initial_type_),int(initial_encoding_));
    }
    CString Name() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentName();
#else
        return m_name;
#endif
    }
    void Name(const CString& value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentName(value);
#else
        m_name=value;
#endif
    }
    CString Path() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetDocumentPath();
#else
        return m_path;
#endif
    }
    void Path(const CString& value) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        SetDocumentPath(value);
#else
        m_path=value;
#endif
    }
    size_t Entries() const {
#ifdef XY_DOCUMENT_PINNED_REFERENCE
        return m_entries.GetCount();
#else
        return GetEntryCount();
#endif
    }
    size_t Segments() const {
#ifdef XY_DOCUMENT_PINNED_REFERENCE
        return m_segments.GetCount();
#else
        return GetSegmentCount();
#endif
    }
    STSEntry Entry(int index) const {
#ifdef XY_DOCUMENT_PINNED_REFERENCE
        return m_entries.GetAt(index);
#else
        return GetEntrySnapshot(index);
#endif
    }
    STSSegment Segment(int index) const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        STSSegment value;check(m_document->Segment(index,value),"expected segment");return value;
#else
        return m_segments.GetAt(index);
#endif
    }
    void Animate(int index) {
#ifdef XY_DOCUMENT_PINNED_REFERENCE
        m_segments.GetAt(index).animated=true;
#else
        OrSegmentAnimated(index,true);
#endif
    }
    size_t NamedCount() const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return GetStyleCount();
#else
        return m_styles.GetCount();
#endif
    }
    bool HasStoredName(const CString& expected) const {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        for(size_t i=0;i<NamedCount();++i){CString actual;check(GetStyleNameAt(i,actual),"stored key ordinal");
#else
        POSITION pos=m_styles.GetStartPosition();while(pos){CString actual;STSStyle* ignored=NULL;m_styles.GetNextAssoc(pos,actual,ignored);
#endif
            if(actual.GetLength()!=expected.GetLength())continue;
            bool equal=true;for(int i=0;i<actual.GetLength();++i)if(actual[i]!=expected[i])equal=false;
            if(equal)return true;
        }
        return false;
    }
    bool CopyNames(TraceDocument& source, bool append) {
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        return CopyNamedStylesFrom(source,append);
#else
        return CSubtitleEditorDlg::CopyNames(*this,source,append);
#endif
    }
    void Dump(const char* stage) const {
        printf("state %u %u %s %zu %zu\n",++sequence,id_,stage,Entries(),Segments());
        printf("metadata %d",m_simple);units(Name());
        printf(" %lu %d %d %d",static_cast<unsigned long>(LCIDValue()),int(Type()),Mode(),int(Encoding()));units(Path());
        printf(" %ld %ld %d %d %d %ld %ld %d %d",Screen().cx,Screen().cy,
            Wrap(),Collisions(),Scaled(),LayoutSize().cx,LayoutSize().cy,
            Forced(),int(PARType()));
        real(PAR());printf(" %d %d\n",int(Matrix()),int(Range()));
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        style(m_document->DefaultStyle());
#else
        style(m_defaultStyle);
#endif
        printf("styles %zu\n",NamedCount());
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        for(size_t i=0;i<NamedCount();++i){CString name;STSStyle value;
            check(GetStyleNameAt(i,name)&&GetStyleValueAt(i,value),"named ordinal read");
            printf("name");units(name);printf("\n");style(value);}
#else
        POSITION pos=m_styles.GetStartPosition();
        while(pos){CString name;STSStyle* value=NULL;m_styles.GetNextAssoc(pos,name,value);printf("name");units(name);printf("\n");style(*value);}
#endif
        for(size_t i=0;i<Entries();i++) {
            const STSEntry e=Entry(int(i));printf("entry %zu",i);units(e.str);units(e.style);units(e.actor);units(e.effect);
            printf(" %d %ld %ld %ld %ld %d %lld %lld %d\n",e.fUnicode,e.marginRect.left,e.marginRect.top,
                e.marginRect.right,e.marginRect.bottom,e.layer,e.start,e.end,e.readorder);
        }
        for(size_t i=0;i<Segments();i++) {
            const STSSegment s=Segment(int(i));printf("segment %zu %lld %lld %d %zu",i,s.start,s.end,s.animated,s.subs.GetCount());
            for(size_t j=0;j<s.subs.GetCount();j++)printf(" %d",s.subs[j]);printf("\n");
        }
    }
    void Empty() override {
        ++empty_events[id_];printf("event %u %u empty-enter\n",++sequence,id_);Dump("before-empty");
        CSimpleTextSubtitle::Empty();
        printf("event %u %u empty-exit\n",++sequence,id_);Dump("after-empty");
    }
protected:
    void OnChanged() override {
        last_callback_metadata_=Metadata();
        last_callback_screen_=Screen();
        last_callback_layout_size_=LayoutSize();
        ++change_events[id_];printf("event %u %u changed-enter\n",++sequence,id_);Dump("callback-state");
        // Actual native read operations reenter after rebuild's Rust loan ends.
        // They never mutate/reallocate reference storage during the callback.
        printf("callback query %u %d %d %d\n",id_,SearchSub(-1,25.0),SearchSub(0,25.0),SearchSub(4,25.0));
        for(size_t i=0;i<Entries();i++) {
            CStringW wide=GetStrW(int(i),true),packed=GetStrWA(int(i),true);
            printf("text callback %u %zu %d",id_,i,IsEntryUnicode(int(i)));units(wide);units(packed);printf("\n");
        }
        printf("event %u %u changed-exit\n",++sequence,id_);
    }
};

static void populate(TraceDocument& doc) {
    STSStyle* normal=new STSStyle;normal->charSet=ANSI_CHARSET;doc.AddStyle(CString(L"Default"),normal);
    STSStyle* japanese=new STSStyle;japanese->charSet=SHIFTJIS_CHARSET;doc.AddStyle(CString(L"Japanese"),japanese);
    doc.Add(CStringW(L" first\nline "),true,0,10000000,CString(L"Default"),CString(L"actor"),CString(L""),CRect(-1,2,3,4),2,9);
    const wchar_t packed[]={0x82,0xa0};
    doc.Add(CStringW(packed,2),false,2000000,8000000,CString(L"Japanese"),CString(L""),CString(L""),CRect(0,0,0,0),1,0);
    doc.AddSTSEntryOnly(CStringW(L"marker"),true,4000000,4000000);
    // Genuine AddStyle renames the original map value and matching entries.
    STSStyle* replacement=new STSStyle;replacement->charSet=SHIFTJIS_CHARSET;replacement->fontSize=21;
    doc.AddStyle(CString(L"Japanese"),replacement);
    doc.CreateSegments();doc.Animate(0);doc.Dump("populated");
}
static void text_edits(TraceDocument& doc) {
    unsigned notifications=change_events[1];
    const wchar_t raw[]={L'R',0,0xd800,L'\n',L'Z'};
    doc.SetStr(0,CStringW(raw,5),true);doc.Dump("setstr-retain-unicode");
    check(doc.IsEntryUnicode(0),"SetStr retains stored Unicode flag");
    // Known Windows charsets, normal valid Shift-JIS byte sequence; native
    // TranslateCharsetInfo and codepage conversions execute in both binaries.
    doc.ConvertUnicode(1,true);doc.Dump("convert-sjis-to-unicode");
    doc.ConvertUnicode(1,true);doc.Dump("convert-unicode-noop");
    doc.SetStr(1,CStringW(L"edited\ntext"),false);doc.Dump("setstr-input-packed-store-unicode");
    check(doc.IsEntryUnicode(1),"SetStr never toggles stored Unicode flag");
    doc.ConvertUnicode(1,false);doc.Dump("convert-unicode-to-sjis");
    doc.ConvertUnicode(1,false);doc.Dump("convert-packed-noop");
    doc.SetStr(1,CStringW(L"plain\ntext"),true);doc.Dump("setstr-input-unicode-store-packed");
    check(!doc.IsEntryUnicode(1),"SetStr retains packed storage flag");
    check(change_events[1]==notifications,"text edits have no OnChanged");
}
static void check_default(TraceDocument& doc,const STSStyle& expected,const char* stage) {
    STSStyle actual;check(doc.GetDefaultStyle(actual),"GetDefaultStyle status");
    // Full initialized values are serialized for direct pinned comparison;
    // these assertions additionally detect loss of raw CString suffix/zero sign.
    check(actual.fontName.GetLength()==expected.fontName.GetLength(),"default font logical length");
    for(int i=0;i<expected.fontName.GetLength();++i)check(actual.fontName[i]==expected.fontName[i],"default raw font unit");
    uint64_t actualBits,expectedBits;std::memcpy(&actualBits,&actual.fontSize,8);std::memcpy(&expectedBits,&expected.fontSize,8);
    check(actualBits==expectedBits&&actual.charSet==expected.charSet,"default scalar bits");
    printf("default-value %s ",stage);style(actual);doc.Dump(stage);
}
static void resolved(TraceDocument& doc,int index,const STSStyle& expected,const char* stage) {
    STSStyle actual;check(doc.GetStyle(index,&actual),"GetStyle copy status");
    check(actual==expected,"resolved initialized style value");
    check(doc.GetCharSet(index)==expected.charSet,"resolved charset");
    printf("resolved-style %s charset=%d ",stage,doc.GetCharSet(index));style(actual);
}
static void default_style_lifecycle() {
    TraceDocument source(0),destination(1);
    STSStyle supplied;const wchar_t raw[]={L'F',0,0xd800,0xdcff,L'Z'};
    supplied.fontName=CString(raw,5);supplied.fontSize=-0.0;supplied.charSet=SHIFTJIS_CHARSET;
    supplied.fontSpacing=-1.25;supplied.fontShiftY=2.5;supplied.marginRect=CRect(-3,4,5,6);
    unsigned notifications=change_events[0];
    check(source.SetDefaultStyle(supplied),"SetDefaultStyle status");
    check(source.SetDefaultStyle(supplied),"SetDefaultStyle identical still true");
    check_default(source,supplied,"default-supplied");
    STSStyle isolated;check(source.GetDefaultStyle(isolated),"default owned read");
    isolated.fontName.SetAt(4,L'Q');isolated.fontSize=11;
    check_default(source,supplied,"default-read-independent");
    source.Empty();check_default(source,supplied,"default-retained-empty");
    check(change_events[0]==notifications,"default set/get/Empty do not notify");
    destination.Copy(source);check_default(destination,supplied,"default-copied");
    STSStyle replacement=supplied;replacement.fontName=CString(L"replacement");replacement.fontSize=31;replacement.charSet=ANSI_CHARSET;
    check(destination.SetDefaultStyle(replacement),"replace copied default");
    check_default(source,supplied,"default-copy-source-independent");
    destination.Copy(destination);check_default(destination,replacement,"default-self-copy-retained");
    // Valid indices only, with no named styles: native default fallback.
    source.AddSTSEntryOnly(CStringW(L"default resolution"),true,0,10,CString(L"Missing"));
    resolved(source,0,supplied,"default-fallback");
    STSStyle* mappedDefault=new STSStyle;mappedDefault->charSet=RUSSIAN_CHARSET;mappedDefault->fontSize=23;
    STSStyle expectedMapped=*mappedDefault;source.AddStyle(CString(L"Default"),mappedDefault);
    resolved(source,0,expectedMapped,"mapped-default-fallback");
    STSStyle* named=new STSStyle;named->charSet=GREEK_CHARSET;named->fontSize=24;
    STSStyle expectedNamed=*named;source.AddStyle(CString(L"Named"),named);
    source.AddSTSEntryOnly(CStringW(L"named resolution"),true,10,20,CString(L"Named"));
    resolved(source,1,expectedNamed,"mapped-named");
    source.SetForceDefaultStyle(true);resolved(source,0,supplied,"forced-missing");resolved(source,1,supplied,"forced-named");
    source.SetForceDefaultStyle(false);resolved(source,1,expectedNamed,"unforced-named");
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
    source.ResetDefaultStyle(HANGEUL_CHARSET);
#else
    source.CreateDefaultStyle(HANGEUL_CHARSET);
#endif
    STSStyle reset;reset.charSet=HANGEUL_CHARSET;check_default(source,reset,"default-reset");
    source.SetForceDefaultStyle(true);resolved(source,1,reset,"forced-reset");
    check(change_events[0]==notifications,"default resolution/reset do not notify");
    source.Dump("default-resolution-final");
}
static STSStyle named_value(int id) {
    STSStyle result;result.charSet=ANSI_CHARSET;result.fontSize=20+id;
    result.fontSpacing=id*0.25;result.colors[0]=0x112200+id;
    const wchar_t raw[]={L'N',0,static_cast<wchar_t>(0xd800+id),L'Z'};
    result.fontName=CString(raw,4);return result;
}
static void named_add(TraceDocument& doc,const wchar_t* name,const STSStyle& value) {
    STSStyle* owned=new STSStyle;*owned=value;doc.AddStyle(CString(name),owned);
}
static void named_stage(TraceDocument& doc,const char* label) {
    printf("named-stage %s\n",label);doc.Dump(label);
}
static std::vector<STSSegment> segment_snapshot(const TraceDocument& doc) {
    std::vector<STSSegment> result;for(size_t i=0;i<doc.Segments();++i)result.push_back(doc.Segment(int(i)));
    check(!result.empty()&&result[0].animated,"style fixture has real animated segments");return result;
}
static void require_same_segments(const TraceDocument& doc,const std::vector<STSSegment>& before,const char* stage) {
    check(doc.Segments()==before.size(),"style conflict segment count unchanged");unsigned animated=0;
    for(size_t i=0;i<before.size();++i){const STSSegment actual=doc.Segment(int(i));const STSSegment& expected=before[i];
        check(actual.start==expected.start&&actual.end==expected.end&&actual.animated==expected.animated&&actual.subs.GetCount()==expected.subs.GetCount(),"style conflict complete segment header unchanged");
        for(size_t j=0;j<expected.subs.GetCount();++j)check(actual.subs[j]==expected.subs[j],"style conflict segment membership unchanged");
        if(actual.animated)++animated;
    }
    check(animated>0,"style conflict animation retained");
    printf("style-rename-segments %s count=%zu animated=%u\n",stage,before.size(),animated);
}
static void style_rename_stage(TraceDocument& doc,const char* stage) {
    printf("style-rename-stage %s\n",stage);doc.Dump(stage);
}
static void named_style_lifecycle() {
    TraceDocument source(0),destination(1);
    const STSStyle first=named_value(1),second=named_value(2),third=named_value(3);
    named_add(source,L"Named",first);named_add(source,L"Named2",second);named_add(source,L"Named3",third);
    source.AddSTSEntryOnly(CStringW(L"rename target"),true,0,10,CString(L"Named"));
    source.AddSTSEntryOnly(CStringW(L"collision stays"),true,10,20,CString(L"Named2"));
    source.CreateSegments();source.Animate(0);const auto stableSegments=segment_snapshot(source);
    unsigned before0=change_events[0],before1=change_events[1];
    named_stage(source,"named-initial");
    // Native CString font equality ignores the suffix, but retained storage must
    // keep the original full font units when the incoming equal value is freed.
    STSStyle equal=first;equal.fontName.SetAt(2,0xdcff);
    check(equal==first,"native equal duplicate fixture");
    named_add(source,L"Named",equal);check(source.NamedCount()==3,"equal duplicate count");
    named_stage(source,"named-equal-duplicate");
    named_add(source,L"Named",second);check(source.NamedCount()==4,"conflict count");
    check(source.Entry(0).style==L"Named4"&&source.Entry(1).style==L"Named2","collision rename feedback");
    named_stage(source,"named-conflict-collision");
    source.AddSTSEntryOnly(CStringW(L"second rename"),true,20,30,CString(L"Named"));
    named_add(source,L"Named",third);check(source.Entry(2).style==L"Named5","second suffix collision");
    named_add(source,L"Named9",first);named_add(source,L"Named10",second);
    source.AddSTSEntryOnly(CStringW(L"numeric suffix"),true,30,40,CString(L"Named9"));
    named_add(source,L"Named9",third);check(source.Entry(3).style==L"Named11","numeric suffix collision");
    named_stage(source,"named-numeric-collision");
    require_same_segments(source,stableSegments,"numeric-original");
    source.AddSTSEntryOnly(CStringW(L"numeric repeat twelve"),true,40,50,CString(L"Named9"));
    named_add(source,L"Named9",first);
    check(source.Entry(4).style==L"Named12"&&source.Entry(3).style==L"Named11","repeated numeric conflict chooses twelve");
    require_same_segments(source,stableSegments,"numeric-repeat-12");style_rename_stage(source,"numeric-repeat-12");
    source.AddSTSEntryOnly(CStringW(L"numeric repeat thirteen"),true,50,60,CString(L"Named9"));
    named_add(source,L"Named9",second);
    check(source.Entry(5).style==L"Named13"&&source.Entry(4).style==L"Named12"&&source.Entry(3).style==L"Named11","repeated numeric conflict chooses thirteen");
    require_same_segments(source,stableSegments,"numeric-repeat-13");style_rename_stage(source,"numeric-repeat-13");
    check(destination.CopyNames(source,false),"replace named copy");
    named_stage(destination,"named-copy-replace");source.Dump("named-copy-source");
    named_add(destination,L"Named",first);
    named_stage(destination,"named-copy-independent");source.Dump("named-independent-source");
    // Append routes every row through actual AddStyle, preserving map traversal,
    // duplicate equality and conflict rename sequencing.
    check(destination.CopyNames(source,true),"append named copy");
    named_stage(destination,"named-copy-append");source.Dump("named-append-source");
    check(destination.CopyNames(destination,false),"self named replace");
    check(destination.NamedCount()==0,"self named replace clears before reading");
    named_stage(destination,"named-self-clear");
    check(destination.CopyNames(source,false),"copy after self clear");
    destination.Empty();check(destination.NamedCount()==0,"Empty frees named styles");
    named_stage(destination,"named-empty");
    require_same_segments(source,stableSegments,"named-final");
    check(change_events[0]==before0&&change_events[1]==before1,"named operations do not notify");
}
// CTextFile deliberately inherits CStdioFile64 protected in both checkouts.
// Expose only the fixture label operation; real Open/parsing remains unchanged.
class FixtureTextFile : public CTextFile {
public:
    explicit FixtureTextFile(CTextFile::enc encoding):CTextFile(encoding){}
    void SetFixturePath(const CString& value) {SetFilePath(value);}
};
struct SavedFiles {
    CString name;
    explicit SavedFiles(const wchar_t* value):name(value){DeleteFile(name);DeleteFile(name+L".style");}
    ~SavedFiles(){DeleteFile(name);DeleteFile(name+L".style");}
};
static void saved_bytes(const CString& path,const char* label,bool expected) {
    const bool exists=GetFileAttributes(path)!=INVALID_FILE_ATTRIBUTES;
    printf("saved-file %s exists=%d",label,exists);check(exists==expected,"export file presence");
    if(!exists){printf("\n");return;}
    CFile file;check(file.Open(path,CFile::modeRead|CFile::typeBinary)!=FALSE,"read saved file");
    const ULONGLONG length=file.GetLength();check(length>0&&length<=1024*1024,"saved file exceeds bounded trace or is empty");
    std::vector<BYTE> bytes(static_cast<size_t>(length));
    check(file.Read(bytes.data(),static_cast<UINT>(bytes.size()))==bytes.size(),"short saved file read");
    printf(" bytes=%zu",bytes.size());for(BYTE byte:bytes)printf(" %02x",unsigned(byte));printf("\n");
}
static void named_style_exports() {
    TraceDocument document(0);STSStyle first=named_value(1),second=named_value(2);
    first.fontName=L"Arial";second.fontName=L"Courier New";
    named_add(document,L"Default",first);named_add(document,L"Export",second);
    document.AddSTSEntryOnly(CStringW(L"ordinary export\nsecond line"),true,1000000,20000000,CString(L"Export"),CString(L"actor"));
    document.AddSTSEntryOnly(CStringW(L"default export"),true,21000000,30000000,CString(L"Default"));
    const exttype types[]={EXTSSA,EXTASS,EXTSRT};
    const wchar_t* paths[]={L"document-lifecycle-save.ssa",L"document-lifecycle-save.ass",L"document-lifecycle-save.srt"};
    const char* labels[]={"ssa","ass","srt"};
    for(int i=0;i<3;++i){SavedFiles files(paths[i]);
        check(document.SaveAs(files.name,types[i],25.0,CTextFile::UTF8),"named style SaveAs");
        saved_bytes(files.name,labels[i],true);
        saved_bytes(files.name+L".style",i==0?"ssa-sidecar":i==1?"ass-sidecar":"srt-sidecar",i==2);
    }
    document.Empty();
    document.AddSTSEntryOnly(CStringW(L"without mapped default"),true,0,10000000,CString(L"Default"));
    SavedFiles absent(L"document-lifecycle-no-sidecar.srt");
    check(document.SaveAs(absent.name,EXTSRT,25.0,CTextFile::UTF8),"no mapped style SaveAs");
    saved_bytes(absent.name,"srt-no-map",true);saved_bytes(absent.name+L".style","srt-no-map-sidecar",false);
}
static bool same_identity(const CString& a,const CString& b) {
    if(a.GetLength()!=b.GetLength())return false;
    for(int i=0;i<a.GetLength();++i)if(a[i]!=b[i])return false;
    return true;
}
static void identity_stage(TraceDocument& doc,const char* stage,const CString& name,const CString& path) {
    check(same_identity(doc.Name(),name)&&same_identity(doc.Path(),path),"complete raw identity values");
    printf("identity-stage %s name",stage);units(doc.Name());printf(" path");units(doc.Path());printf("\n");
    doc.Dump(stage);
}
static void identity_lifecycle() {
    TraceDocument source(0),destination(1);
    const wchar_t rawName[]={L'N',0,0xd800,L'X'},rawPath[]={L'P',0,0xdcff,L'Y'};
    const CString name(rawName,4),path(rawPath,4),destinationPath(L"destination-retained-path");
    source.Name(name);source.Path(path);destination.Path(destinationPath);
    unsigned sourceChanges=change_events[0],destinationChanges=change_events[1];
    identity_stage(source,"identity-initial",name,path);
    CString isolatedName=source.Name(),isolatedPath=source.Path();
    isolatedName.SetAt(3,L'Q');isolatedPath.SetAt(3,L'R');
    identity_stage(source,"identity-read-independent",name,path);
    destination.Copy(source);
    identity_stage(destination,"identity-copy-asymmetric-path",name,destinationPath);
    destination.Name(CString(L"edited destination"));destination.Path(CString(L"edited destination path"));
    identity_stage(source,"identity-copy-independent-source",name,path);
    source.AddSTSEntryOnly(CStringW(L"identity callback"),true,0,10000000);
    source.CreateSegments(); // callback Dump and reads see the initialized identity
    ++sourceChanges;
    identity_stage(source,"identity-callback",name,path);
    source.RemoveAllEntries();identity_stage(source,"identity-remove-entries",name,path);
    source.Empty();identity_stage(source,"identity-empty",name,path);
    source.Copy(source);identity_stage(source,"identity-self-copy",name,path);
    check(change_events[0]==sourceChanges&&change_events[1]==destinationChanges,"identity operations add no notifications");
    SavedFiles fixture(L"document-lifecycle-identity.srt");
    {
        const char bytes[]="1\r\n00:00:00,000 --> 00:00:01,000\r\nordinary identity open\r\n\r\n";
        CFile output;check(output.Open(fixture.name,CFile::modeCreate|CFile::modeWrite|CFile::typeBinary)!=FALSE,"identity fixture create");
        output.Write(bytes,sizeof(bytes)-1);
    }
    FixtureTextFile input(CTextFile::UTF8);check(input.Open(fixture.name),"identity fixture input");
    // CFile's public label setter avoids machine-specific absolute cwd in the
    // paired trace; parsing and actual CSimpleTextSubtitle::Open remain native.
    input.SetFixturePath(fixture.name);
    check(input.GetFilePath()==fixture.name,"deterministic opened file label");
    check(source.Open(&input,ANSI_CHARSET,CString(L"opened identity")),"ordinary identity Open");
    identity_stage(source,"identity-open-success",CString(L"opened identity"),fixture.name);
}
static void interpretation_stage(TraceDocument& doc,const char* stage,int mode,exttype type,CTextFile::enc encoding) {
    check(doc.Mode()==mode&&doc.Type()==type&&doc.Encoding()==encoding,"interpretation scalar values");
    printf("interpretation-stage %s mode=%d type=%d encoding=%d\n",stage,doc.Mode(),int(doc.Type()),int(doc.Encoding()));
    doc.Dump(stage);
}
static void interpretation_lifecycle() {
    TraceDocument source(0),destination(1);source.ConstructionInterpretation();
    unsigned sourceChanges=change_events[0],destinationChanges=change_events[1];
    source.Mode(FRAME);source.Type(EXTASS);source.Encoding(CTextFile::LE16);
    interpretation_stage(source,"interpretation-supplied",FRAME,EXTASS,CTextFile::LE16);
    destination.Copy(source);
    interpretation_stage(destination,"interpretation-copy",FRAME,EXTASS,CTextFile::LE16);
    destination.Mode(TIME);destination.Type(EXTSSA);destination.Encoding(CTextFile::UTF8);
    interpretation_stage(source,"interpretation-copy-independent",FRAME,EXTASS,CTextFile::LE16);
    source.Empty();interpretation_stage(source,"interpretation-empty",FRAME,EXTASS,CTextFile::LE16);
    source.Copy(source);interpretation_stage(source,"interpretation-self-copy",FRAME,EXTASS,CTextFile::LE16);
    check(change_events[0]==sourceChanges&&change_events[1]==destinationChanges,"interpretation scalar operations add no callbacks");
    SavedFiles fixture(L"document-lifecycle-interpretation.sub");
    {
        const char bytes[]="{0}{25}ordinary MicroDVD\r\n{30}{55}second line\r\n";
        CFile output;check(output.Open(fixture.name,CFile::modeCreate|CFile::modeWrite|CFile::typeBinary)!=FALSE,"MicroDVD fixture create");
        output.Write(bytes,sizeof(bytes)-1);
    }
    FixtureTextFile input(CTextFile::UTF8);check(input.Open(fixture.name),"MicroDVD fixture input");input.SetFixturePath(fixture.name);
    check(input.GetFilePath()==fixture.name,"deterministic MicroDVD label");
    check(source.Open(&input,ANSI_CHARSET,CString(L"interpretation MicroDVD")),"ordinary MicroDVD Open");
    interpretation_stage(source,"interpretation-microdvd-open",FRAME,EXTSSA,input.GetEncoding());
}
static void layout_stage(TraceDocument& doc,const char* stage,const CSize& screen,int wrap,int collisions,bool scaled) {
    check(doc.Screen()==screen&&doc.Wrap()==wrap&&doc.Collisions()==collisions&&doc.Scaled()==scaled,"script layout values");
    printf("layout-stage %s width=%ld height=%ld wrap=%d collisions=%d scaled=%d\n",stage,doc.Screen().cx,doc.Screen().cy,doc.Wrap(),doc.Collisions(),doc.Scaled());doc.Dump(stage);
}
static CStringA ass_fixture(const char* headers) {
    CStringA text("[Script Info]\r\nScriptType: v4.00+\r\n");text+=headers;
    text+="[V4+ Styles]\r\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\nStyle: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\nDialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,ordinary layout\r\n";
    return text;
}
static void layout_open(const char* label,const char* headers,const CSize& expected,int wrap,int collisions,bool scaled,const CSize& callbackSize) {
    TraceDocument doc(0);SavedFiles fixture(L"document-lifecycle-layout.ass");
    const CStringA text=ass_fixture(headers);
    {CFile output;check(output.Open(fixture.name,CFile::modeCreate|CFile::modeWrite|CFile::typeBinary)!=FALSE,"layout fixture create");output.Write(text.GetString(),text.GetLength());}
    FixtureTextFile input(CTextFile::UTF8);check(input.Open(fixture.name),"layout fixture input");input.SetFixturePath(fixture.name);
    check(input.GetFilePath()==fixture.name,"deterministic layout label");unsigned before=change_events[0];
    check(doc.Open(&input,ANSI_CHARSET,CString(L"layout fixture")),"ordinary ASS layout Open");
    check(change_events[0]==before+1&&doc.LastCallbackScreen()==callbackSize,"Open callback sees resolution before fallback");
    printf("layout-callback %s width=%ld height=%ld\n",label,doc.LastCallbackScreen().cx,doc.LastCallbackScreen().cy);
    layout_stage(doc,label,expected,wrap,collisions,scaled);
}
static void layout_lifecycle() {
    TraceDocument source(0),destination(1);source.ConstructionLayout();
    const unsigned sourceChanges=change_events[0],destinationChanges=change_events[1];
    source.Screen(CSize(-4,12));source.Wrap(7);source.Collisions(1);source.Scaled(true);
    layout_stage(source,"layout-supplied",CSize(-4,12),7,1,true);
    destination.Copy(source);layout_stage(destination,"layout-copy",CSize(-4,12),7,1,true);
    destination.Screen(CSize(16,20));destination.Wrap(-2);destination.Collisions(0);destination.Scaled(false);
    layout_stage(source,"layout-copy-independent",CSize(-4,12),7,1,true);
    source.Empty();layout_stage(source,"layout-empty",CSize(0,0),7,1,true);
    source.Screen(CSize(32,24));source.Copy(source);layout_stage(source,"layout-self-copy",CSize(0,0),7,1,true);
    check(change_events[0]==sourceChanges&&change_events[1]==destinationChanges,"layout scalar lifecycle adds no callbacks");
    layout_open("layout-open-fallback","",CSize(384,288),0,0,false,CSize(0,0));
    layout_open("layout-open-x1280","PlayResX: 1280\r\n",CSize(1280,1024),0,0,false,CSize(1280,1024));
    layout_open("layout-open-y1024","PlayResY: 1024\r\n",CSize(1280,1024),0,0,false,CSize(1280,1024));
    layout_open("layout-open-repeated","PlayResX: 1280\r\nPlayResY: 600\r\nPlayResX: -8\r\nWrapStyle: -2\r\nWrapStyle: 7\r\nCollisions: prefixREVERSEsuffix\r\nScaledBorderAndShadow: notYESexact\r\n",CSize(-8,600),7,1,true,CSize(-8,600));
}
static bool same_metadata(const MetadataValue& a,const MetadataValue& b) {
    uint64_t left,right;std::memcpy(&left,&a.par,8);std::memcpy(&right,&b.par,8);
    return a.forced==b.forced&&a.parType==b.parType&&left==right&&a.matrix==b.matrix&&a.range==b.range;
}
static void metadata_line(const char* prefix,const char* stage,const MetadataValue& value) {
    printf("%s %s forced=%d par-type=%d par-bits",prefix,stage,value.forced,int(value.parType));real(value.par);
    printf(" matrix=%d range=%d\n",int(value.matrix),int(value.range));
}
static void metadata_stage(TraceDocument& doc,const char* stage,const MetadataValue& expected) {
    check(same_metadata(doc.Metadata(),expected),"initialized metadata values");
    metadata_line("metadata-stage",stage,doc.Metadata());doc.Dump(stage);
}
static void metadata_open(const char* stage,const char* headers,bool ass,bool microdvd,
                          CSimpleTextSubtitle::YCbCrMatrix matrix,CSimpleTextSubtitle::YCbCrRange range) {
    TraceDocument doc(0);doc.Matrix(CSimpleTextSubtitle::YCbCrMatrix_BT2020);doc.Range(CSimpleTextSubtitle::YCbCrRange_PC);
    SavedFiles fixture(L"document-lifecycle-metadata.sub");
    const CStringA text=ass?ass_fixture(headers):CStringA(microdvd?"{0}{25}ordinary metadata\r\n":"1\r\n00:00:00,000 --> 00:00:01,000\r\nordinary metadata\r\n\r\n");
    {CFile output;check(output.Open(fixture.name,CFile::modeCreate|CFile::modeWrite|CFile::typeBinary)!=FALSE,"metadata fixture create");output.Write(text.GetString(),text.GetLength());}
    FixtureTextFile input(CTextFile::UTF8);check(input.Open(fixture.name),"metadata fixture input");input.SetFixturePath(fixture.name);
    check(input.GetFilePath()==fixture.name,"deterministic metadata label");unsigned before=change_events[0];
    check(doc.Open(&input,ANSI_CHARSET,CString(L"metadata fixture")),"ordinary metadata Open");
    const MetadataValue expected={false,CSimpleTextSubtitle::EPCTDownscale,1.25,matrix,range};
    check(change_events[0]==before+1&&same_metadata(doc.CallbackMetadata(),expected),"Open callback metadata");
    metadata_line("metadata-callback",stage,doc.CallbackMetadata());metadata_stage(doc,stage,expected);
}
static void metadata_lifecycle() {
    TraceDocument source(0),destination(1);
    const MetadataValue initial={false,CSimpleTextSubtitle::EPCTDisabled,1.0,CSimpleTextSubtitle::YCbCrMatrix_AUTO,CSimpleTextSubtitle::YCbCrRange_AUTO};
    check(same_metadata(source.InitialMetadata(),initial),"actual constructor metadata defaults");
    metadata_line("metadata-stage","metadata-constructed",source.InitialMetadata());
    source.SetForceDefaultStyle(true);source.PARType(CSimpleTextSubtitle::EPCTAccurateSize);source.PAR(1.5);
    source.Matrix(CSimpleTextSubtitle::YCbCrMatrix_BT2020);source.Range(CSimpleTextSubtitle::YCbCrRange_TV);
    const MetadataValue supplied=source.Metadata();metadata_stage(source,"metadata-supplied",supplied);
    destination.Forced(false);destination.PARType(CSimpleTextSubtitle::EPCTUpscale);destination.PAR(2.5);
    destination.Matrix(CSimpleTextSubtitle::YCbCrMatrix_BT709);destination.Range(CSimpleTextSubtitle::YCbCrRange_PC);
    MetadataValue copied=destination.Metadata();copied.forced=true;
    unsigned sourceChanges=change_events[0],destinationChanges=change_events[1];
    destination.Copy(source);metadata_stage(destination,"metadata-copy-forced-only",copied);
    destination.Forced(false);destination.PAR(3.5);metadata_stage(source,"metadata-copy-independent",supplied);
    source.Empty();metadata_stage(source,"metadata-empty",supplied);
    source.Copy(source);metadata_stage(source,"metadata-self-copy",supplied);
    STSStyle suppliedDefault;suppliedDefault.charSet=SHIFTJIS_CHARSET;suppliedDefault.fontSize=29;
    check(source.SetDefaultStyle(suppliedDefault),"metadata default setup");
    STSStyle* mapped=new STSStyle;mapped->charSet=GREEK_CHARSET;STSStyle mappedValue=*mapped;source.AddStyle(CString(L"Default"),mapped);
    source.AddSTSEntryOnly(CStringW(L"metadata style"),true,0,10000000);
    resolved(source,0,suppliedDefault,"metadata-forced-default");metadata_stage(source,"metadata-forced-style",supplied);
    source.SetForceDefaultStyle(false);resolved(source,0,mappedValue,"metadata-unforced-mapped");
    MetadataValue unforced=supplied;unforced.forced=false;metadata_stage(source,"metadata-unforced-style",unforced);
    check(change_events[0]==sourceChanges&&change_events[1]==destinationChanges,"metadata set/copy/empty/style lookups do not notify");
    using Doc=CSimpleTextSubtitle;
    metadata_open("metadata-color-none","YCbCr Matrix: NONEsuffix\r\n",true,false,Doc::YCbCrMatrix_AUTO,Doc::YCbCrRange_AUTO);
    metadata_open("metadata-color-tv601","YCbCr Matrix: TV.601suffix\r\n",true,false,Doc::YCbCrMatrix_BT601,Doc::YCbCrRange_TV);
    metadata_open("metadata-color-tv709","YCbCr Matrix: TV.709suffix\r\n",true,false,Doc::YCbCrMatrix_BT709,Doc::YCbCrRange_TV);
    metadata_open("metadata-color-tv2020","YCbCr Matrix: TV.2020suffix\r\n",true,false,Doc::YCbCrMatrix_BT2020,Doc::YCbCrRange_TV);
    metadata_open("metadata-color-pc601","YCbCr Matrix: PC.601suffix\r\n",true,false,Doc::YCbCrMatrix_BT601,Doc::YCbCrRange_PC);
    metadata_open("metadata-color-pc709","YCbCr Matrix: PC.709suffix\r\n",true,false,Doc::YCbCrMatrix_BT709,Doc::YCbCrRange_PC);
    metadata_open("metadata-color-pc2020","YCbCr Matrix: PC.2020suffix\r\n",true,false,Doc::YCbCrMatrix_BT2020,Doc::YCbCrRange_PC);
    metadata_open("metadata-color-unrecognized","YCbCr Matrix: unrecognized\r\n",true,false,Doc::YCbCrMatrix_BT601,Doc::YCbCrRange_TV);
    metadata_open("metadata-color-repeated","YCbCr Matrix: tv.601\r\nYCbCr Matrix: pc.709suffix\r\nYCbCr Matrix: unrecognized\r\n",true,false,Doc::YCbCrMatrix_BT709,Doc::YCbCrRange_PC);
    metadata_open("metadata-fallback-srt","",false,false,Doc::YCbCrMatrix_BT601,Doc::YCbCrRange_TV);
    metadata_open("metadata-fallback-microdvd","",false,true,Doc::YCbCrMatrix_BT601,Doc::YCbCrRange_TV);
}
static void copy_state_stage(TraceDocument& doc,const char* stage) {
    printf("copy-state-stage %s\n",stage);doc.Dump(stage);
}
static STSStyle callback_default_style() {
    STSStyle value;const wchar_t font[]={L'F',0,0xd800,L'C'};
    value.fontName=CString(font,4);value.fontSize=27;value.charSet=SHIFTJIS_CHARSET;return value;
}
class CopyMutationDocument : public TraceDocument {
    TraceDocument* source_=nullptr;
public:
    CopyMutationDocument():TraceDocument(1){}
    void Arm(TraceDocument& source){source_=&source;}
    void Empty() override {
        TraceDocument::Empty();
        if(!source_)return;
        TraceDocument& source=*source_;source_=nullptr;
        const wchar_t name[]={L'A',0,0xd800,L'C'};source.Name(CString(name,4));
        source.Mode(FRAME);source.Type(EXTASS);source.Encoding(CTextFile::LE16);
        source.Screen(CSize(320,240));source.Wrap(7);source.Collisions(1);source.Scaled(true);
        STSStyle value=callback_default_style();check(source.SetDefaultStyle(value),"callback default assignment");
        source.SetForceDefaultStyle(true);
        source.Path(CString(L"source path after callback"));source.PARType(EPCTAccurateSize);source.PAR(1.5);
        source.Matrix(YCbCrMatrix_BT2020);source.Range(YCbCrRange_TV);
        copy_state_stage(source,"copy-source-mutated");
    }
};
static void copy_selected_lifecycle() {
    {
        TraceDocument source(0);CopyMutationDocument destination;
        source.AddSTSEntryOnly(CStringW(L"callback copy source"),true,0,10);
        STSStyle* named=new STSStyle;named->fontSize=18;source.AddStyle(CString(L"Default"),named);
        destination.AddSTSEntryOnly(CStringW(L"old destination"),true,0,20);
        destination.Path(CString(L"destination retained path"));destination.PARType(CSimpleTextSubtitle::EPCTUpscale);destination.PAR(2.5);
        const CString retainedPath=destination.Path();MetadataValue retained=destination.Metadata();retained.forced=true;
        const unsigned sourceChanges=change_events[0],destinationChanges=change_events[1];
        copy_state_stage(source,"copy-source-before");destination.Arm(source);destination.Copy(source);
        const wchar_t name[]={L'A',0,0xd800,L'C'};
        check(same_identity(destination.Name(),CString(name,4)),"Copy reads source name after virtual Empty");
        check(destination.Mode()==FRAME&&destination.Type()==EXTASS&&destination.Encoding()==CTextFile::LE16,"Copy reads source interpretation after virtual Empty");
        check(destination.Screen()==CSize(320,240)&&destination.Wrap()==7&&destination.Collisions()==1&&destination.Scaled(),"Copy reads source layout after virtual Empty");
        check(same_identity(destination.Path(),retainedPath)&&same_metadata(destination.Metadata(),retained),"Copy retains destination path/PAR/colors");
        check(destination.Entries()==1&&destination.Entry(0).str==L"callback copy source"&&destination.NamedCount()==1,"Copy map and storage tail remain native orchestration");
        check_default(destination,callback_default_style(),"copy-callback-default");
        copy_state_stage(destination,"copy-destination-after");copy_state_stage(source,"copy-source-after");
        check(change_events[0]==sourceChanges&&change_events[1]==destinationChanges,"Copy callback fixture has no OnChanged");
    }
    {
        TraceDocument source(0),destination(1);const wchar_t raw[]={0,L'K'};const CString leadingNul(raw,2);
        STSStyle* first=new STSStyle;first->fontSize=18;source.AddStyle(CString(L"Default"),first);
        STSStyle* second=new STSStyle;second->fontSize=19;source.AddStyle(leadingNul,second);
        source.AddSTSEntryOnly(CStringW(L"empty stored key"),true,0,10,leadingNul);
        source.AddSTSEntryOnly(CStringW(L"mapped default key"),true,20,30,CString(L"Default"));
        check(leadingNul.GetLength()==2&&source.NamedCount()==2&&source.HasStoredName(CString())&&source.HasStoredName(CString(L"Default")),"leading NUL inserts empty stored key alongside Default");
        copy_state_stage(source,"copy-empty-key-source");destination.Copy(source);
        check(destination.NamedCount()==2&&destination.HasStoredName(CString(L"Default"))&&!destination.HasStoredName(CString()),"Native Copy AddStyle normalizes empty key and conflicts");
        check(destination.Entries()==2&&destination.Entry(1).style==L"Default","native map stage precedes storage tail entry copy");
        check(destination.Entry(0).style.GetLength()==2&&destination.Entry(0).style[0]==0&&destination.Entry(0).style[1]==L'K',"storage tail keeps raw entry style");
        copy_state_stage(destination,"copy-empty-key-result");copy_state_stage(source,"copy-empty-key-source-after");
    }
}
static void prepared_style_conflicts() {
    TraceDocument doc(0);const STSStyle first=named_value(1),second=named_value(2),third=named_value(3);
    const wchar_t rawName[]={L'R',0,L'B',L'0',L'0',L'9'},alternate[]={L'R',0,L'Q',L'1'};
    const wchar_t alias[]={L'R',0,L'X'},newAlias[]={L'R',0,L'Y'};
    const CString name(rawName,6),other(alternate,4);
    STSStyle* owned=new STSStyle;*owned=first;doc.AddStyle(name,owned);
    named_add(doc,L"R10",second);named_add(doc,L"R11",third);
    doc.AddSTSEntryOnly(CStringW(L"prefix equal alias"),true,0,20,CString(alias,3));
    doc.AddSTSEntryOnly(CStringW(L"short prefix"),true,10,30,CString(L"R"));
    doc.AddSTSEntryOnly(CStringW(L"unrelated collision"),true,20,40,CString(L"R10"));
    doc.CreateSegments();doc.Animate(0);const auto stable=segment_snapshot(doc);const unsigned before=change_events[0];
    check(doc.HasStoredName(CString(L"R"))&&!doc.HasStoredName(name),"raw name key conversion preserves only prefix");
    require_same_segments(doc,stable,"raw-nul-before");style_rename_stage(doc,"raw-nul-before");
    owned=new STSStyle;*owned=second;doc.AddStyle(name,owned);
    check(same_identity(doc.Entry(0).style,CString(L"R12"))&&same_identity(doc.Entry(1).style,CString(L"R12"))&&same_identity(doc.Entry(2).style,CString(L"R10")),"full logical numeric suffix selects R12 and prefix aliases rename");
    require_same_segments(doc,stable,"raw-nul-to12");style_rename_stage(doc,"raw-nul-to12");
    doc.AddSTSEntryOnly(CStringW(L"new prefix alias"),true,40,50,CString(newAlias,3));
    owned=new STSStyle;*owned=third;doc.AddStyle(other,owned);
    check(same_identity(doc.Entry(3).style,CString(L"R2"))&&same_identity(doc.Entry(0).style,CString(L"R12"))&&same_identity(doc.Entry(1).style,CString(L"R12")),"caller suffix rather than stored key controls second generated name");
    require_same_segments(doc,stable,"raw-nul-to2");style_rename_stage(doc,"raw-nul-to2");
    check(change_events[0]==before,"prepared style conflicts do not notify");
    {
        TraceDocument shrink(0);
        // 128 crosses the captured SDK's second growth threshold. Qualified
        // base Empty preserves actual clearing/bin retention without dumping
        // the large pre-clear map through this probe's tracing override.
        for(int i=0;i<128;++i){CString key;key.Format(L"Grow%03d",i);STSStyle* value=new STSStyle;*value=named_value(i%3+1);shrink.AddStyle(key,value);}
        shrink.CSimpleTextSubtitle::Empty();check(shrink.NamedCount()==0,"large map cleared");
        style_rename_stage(shrink,"shrink-post-empty");
        named_add(shrink,L"Old",first);named_add(shrink,L"Other",second);
        shrink.AddSTSEntryOnly(CStringW(L"shrink rename"),true,0,10,CString(L"Old"));
        shrink.AddSTSEntryOnly(CStringW(L"shrink unrelated"),true,10,20,CString(L"Other"));
        shrink.CreateSegments();shrink.Animate(0);const auto unchanged=segment_snapshot(shrink);const unsigned callbacks=change_events[0];
        require_same_segments(shrink,unchanged,"shrink-before");style_rename_stage(shrink,"shrink-before");
        named_add(shrink,L"Old",third);
        check(shrink.NamedCount()==3&&same_identity(shrink.Entry(0).style,CString(L"Old2"))&&same_identity(shrink.Entry(1).style,CString(L"Other")),"removal shrink precedes prepared old-style insertion/rename");
        require_same_segments(shrink,unchanged,"shrink-after");style_rename_stage(shrink,"shrink-after");
        check(change_events[0]==callbacks,"removal shrink conflict does not notify");
    }
}
static void add_style_session_lifecycle() {
    {
        TraceDocument doc(0);STSStyle negative=named_value(1),positive=named_value(2);
        negative.fontSize=-0.0;positive.fontSize=0.0;
        named_add(doc,L"ZeroNeg",negative);named_add(doc,L"ZeroPos",positive);
        doc.AddSTSEntryOnly(CStringW(L"negative zero"),true,0,10,CString(L"ZeroNeg"),CString(L"actor"),CString(L"effect"),CRect(1,2,3,4),1,7);
        doc.AddSTSEntryOnly(CStringW(L"positive zero"),true,10,20,CString(L"ZeroPos"),CString(L"actor2"),CString(L"effect2"),CRect(5,6,7,8),2,8);
        doc.CreateSegments();doc.Animate(0);const auto segments=segment_snapshot(doc);const unsigned callbacks=change_events[0];
        printf("add-style-stage add-style-equal-before\n");doc.Dump("add-style-equal-before");
        STSStyle incomingNegative=negative,incomingPositive=positive;
        incomingNegative.fontSize=0.0;incomingPositive.fontSize=-0.0;
        incomingNegative.fontName.SetAt(2,0xdcff);incomingPositive.fontName.SetAt(2,0xdfff);
        check(incomingNegative==negative&&incomingPositive==positive,"native signedzero/postNUL duplicate equality");
        named_add(doc,L"ZeroNeg",incomingNegative);named_add(doc,L"ZeroPos",incomingPositive);
        check(doc.NamedCount()==2&&same_identity(doc.Entry(0).style,CString(L"ZeroNeg"))&&same_identity(doc.Entry(1).style,CString(L"ZeroPos")),"equal duplicates do not rename or grow map");
        const STSStyle expected[]={negative,positive};const char* names[]={"ZeroNeg","ZeroPos"};
        for(int i=0;i<2;++i){STSStyle actual;check(doc.GetStyle(i,&actual),"signedzero retained owned style");uint64_t bits,wanted;
            std::memcpy(&bits,&actual.fontSize,8);std::memcpy(&wanted,&expected[i].fontSize,8);
            check(bits==wanted&&same_identity(actual.fontName,expected[i].fontName),"equal duplicate retains original raw float bits and full font units");
            printf("add-style-retained %s bits=%016llx font",names[i],static_cast<unsigned long long>(bits));units(actual.fontName);printf("\n");}
        require_same_segments(doc,segments,"add-style-equal-after");
        check(change_events[0]==callbacks,"equal duplicates do not notify");
        printf("add-style-stage add-style-equal-after\n");doc.Dump("add-style-equal-after");
    }
    {
        TraceDocument doc(0);STSStyle first=named_value(1),second=named_value(2);const unsigned callbacks=change_events[0];
        STSStyle* owned=new STSStyle;*owned=first;doc.AddStyle(CString(),owned);
        const wchar_t raw[]={0,L'K'};const CString leadingNul(raw,2);
        owned=new STSStyle;*owned=second;doc.AddStyle(leadingNul,owned);
        doc.AddSTSEntryOnly(CStringW(L"logical empty normalized"),true,0,10,CString(L"Default"));
        doc.AddSTSEntryOnly(CStringW(L"nonempty leading NUL"),true,10,20,leadingNul);
        check(doc.NamedCount()==2&&doc.HasStoredName(CString(L"Default"))&&doc.HasStoredName(CString()),"logical empty becomes Default while leading NUL creates empty stored key");
        STSStyle normal,empty;check(doc.GetStyle(0,&normal)&&doc.GetStyle(1,&empty),"separate name class resolutions");
        check(normal==first&&empty==second&&same_identity(doc.Entry(1).style,leadingNul),"name classes retain separate values and raw entry name");
        printf("add-style-stage add-style-name-classes\n");doc.Dump("add-style-name-classes");
        check(change_events[0]==callbacks,"name classification adds no callbacks");
        printf("add-style-name-classes count=%zu default=1 empty=1 default-size=%d empty-size=%d entry",doc.NamedCount(),int(normal.fontSize),int(empty.fontSize));units(doc.Entry(1).style);printf("\n");
    }
}

static void copy_styles_stage(TraceDocument& doc, const char* name) {
    printf("copy-styles-stage %s\n", name); doc.Dump(name);
}
static void copy_styles_session_lifecycle() {
    {
        TraceDocument doc(0);
        STSStyle expected[] = {named_value(1), named_value(2), named_value(3)};
        expected[0].fontSize = -0.0; expected[1].fontSize = 0.0;
        const wchar_t* names[] = {L"SelfA", L"SelfB", L"SelfC"};
        for(int i = 0; i < 3; ++i) {
            named_add(doc, names[i], expected[i]);
            doc.AddSTSEntryOnly(CStringW(L"self append"), true, i*10, (i+1)*10, CString(names[i]));
        }
        doc.CreateSegments(); doc.Animate(0);
        const auto segments = segment_snapshot(doc); const unsigned callbacks = change_events[0];
        copy_styles_stage(doc, "copy-styles-self-before");
        check(doc.CopyNames(doc, true), "ordinary self append succeeds");
        check(doc.NamedCount() == 3 && doc.Entries() == 3, "self append retains map and entries");
        for(int i = 0; i < 3; ++i) {
            STSStyle actual; check(doc.GetStyle(i, &actual), "self append resolves original style");
            uint64_t bits, wanted;
            std::memcpy(&bits, &actual.fontSize, 8); std::memcpy(&wanted, &expected[i].fontSize, 8);
            check(bits == wanted && actual == expected[i] && same_identity(actual.fontName, expected[i].fontName)
                && same_identity(doc.Entry(i).style, CString(names[i])), "self append retains exact original values");
        }
        require_same_segments(doc, segments, "copy-styles-self-after");
        check(change_events[0] == callbacks, "self append adds no callback");
        copy_styles_stage(doc, "copy-styles-self-after");
    }
    {
        TraceDocument source(0), destination(1);
        named_add(source, L"SmallA", named_value(1));
        named_add(source, L"SmallB", named_value(2));
        named_add(source, L"SmallC", named_value(3));
        for(int i = 0; i < 128; ++i) {
            CString name; name.Format(L"Grown%d", i); named_add(destination, name, named_value(4));
        }
        destination.AddSTSEntryOnly(CStringW(L"replacement keeps records"), true, 0, 10, CString(L"SmallA"));
        destination.CreateSegments(); destination.Animate(0);
        const auto segments = segment_snapshot(destination);
        const unsigned sourceCallbacks = change_events[0], destinationCallbacks = change_events[1];
        copy_styles_stage(source, "copy-styles-grown-source-before");
        check(destination.CopyNames(source, false), "replace grown destination map");
        check(destination.NamedCount() == 3 && destination.Entries() == 1, "replacement retains records and copies three styles");
        require_same_segments(destination, segments, "copy-styles-grown-result");
        copy_styles_stage(destination, "copy-styles-grown-result");
        copy_styles_stage(source, "copy-styles-grown-source-after");
        check(change_events[0] == sourceCallbacks && change_events[1] == destinationCallbacks, "grown replacement adds no callbacks");
    }
    {
        TraceDocument source(0), destination(1);
        STSStyle original = named_value(1), equal = original;
        original.fontSize = -0.0; equal.fontSize = 0.0; equal.fontName.SetAt(2, 0xdcff);
        named_add(destination, L"Equal", original);
        named_add(destination, L"Conflict", named_value(1));
        named_add(destination, L"Conflict2", named_value(3));
        named_add(source, L"Equal", equal);
        named_add(source, L"Conflict", named_value(2));
        named_add(source, L"Missing", named_value(3));
        const wchar_t* names[] = {L"Equal", L"Conflict", L"Conflict2"};
        for(int i = 0; i < 3; ++i)
            destination.AddSTSEntryOnly(CStringW(L"mixed append"), true, i*10, (i+1)*10, CString(names[i]));
        destination.CreateSegments(); destination.Animate(0);
        const auto segments = segment_snapshot(destination);
        const unsigned sourceCallbacks = change_events[0], destinationCallbacks = change_events[1];
        copy_styles_stage(source, "copy-styles-mixed-source-before");
        check(destination.CopyNames(source, true), "mixed append succeeds");
        check(destination.NamedCount() == 5 && destination.HasStoredName(CString(L"Missing"))
            && same_identity(destination.Entry(1).style, CString(L"Conflict3"))
            && same_identity(destination.Entry(2).style, CString(L"Conflict2")), "mixed append retains collision sequencing");
        STSStyle actual; check(destination.GetStyle(0, &actual), "mixed append equal style resolves");
        uint64_t bits; std::memcpy(&bits, &actual.fontSize, 8);
        check(bits == UINT64_C(0x8000000000000000) && same_identity(actual.fontName, original.fontName), "mixed equal row retains old full value");
        require_same_segments(destination, segments, "copy-styles-mixed-result");
        copy_styles_stage(destination, "copy-styles-mixed-result");
        copy_styles_stage(source, "copy-styles-mixed-source-after");
        check(change_events[0] == sourceCallbacks && change_events[1] == destinationCallbacks, "mixed append adds no callbacks");
    }
}

class SortDocument : public TraceDocument {
public:
    unsigned notifications=0;
    const char* expected=NULL;
    SortDocument():TraceDocument(0){}
protected:
    void OnChanged() override {
        ++notifications;
        if(!expected)return; // initial real CreateSegments notification
        check(Entries()==12&&Segments()>0,"Sort callback observes rebuilt records");
        printf("sort-callback %s entries=%zu segments=%zu",expected,Entries(),Segments());
        for(size_t i=0;i<Entries();++i){const STSEntry e=Entry(int(i));printf(" %d",e.marginRect.left);}
        printf("\n");
        for(size_t i=0;i<Segments();++i){const STSSegment seg=Segment(int(i));
            check(!seg.animated&&seg.subs.GetCount()>0,"Sort callback rebuilt animation/membership");
            for(size_t j=0;j<seg.subs.GetCount();++j){check(seg.subs[j]>=0&&size_t(seg.subs[j])<Entries(),"Sort callback segment index");
                const STSEntry e=Entry(seg.subs[j]);check(e.start<=seg.start&&e.end>=seg.end,"Sort callback membership observes final records");}}
    }
};
static void sort_lifecycle() {
    SortDocument doc;
    const int starts[12]={20,0,10,10,10,10,0,20,-10,10,0,20};
    const int layers[12]={1,0,2,2,2,-1,1,0,0,2,0,1};
    const int orders[12]={4,3,7,7,7,3,7,3,0,2,3,4};
    std::vector<STSEntry> originals;
    for(int i=0;i<12;++i){
        const wchar_t payload[]={wchar_t(L'A'+i),0,wchar_t(0xd800+i),wchar_t(L'a'+i)};
        const wchar_t name[]={L'S',wchar_t(L'A'+i),0,wchar_t(0xdc00+i)};
        doc.AddSTSEntryOnly(CStringW(payload,4),true,starts[i],starts[i]+30,CString(name,4),
            CString(L"actor"),CString(L"effect"),CRect(i,100+i,200+i,300+i),layers[i],orders[i]);
        originals.push_back(doc.Entry(i));
    }
    doc.CreateSegments();doc.Animate(0);
    printf("sort-stage sort-input\n");doc.Dump("sort-input");
    const char* stages[]={"sort-time","sort-time-repeat","sort-readorder","sort-readorder-repeat"};
    for(int n=0;n<4;++n){
        doc.expected=stages[n];const unsigned before=doc.notifications;
        doc.Sort(n>=2);check(doc.notifications==before+1,"Sort exactly one OnChanged");
        bool seen[12]={};
        for(size_t j=0;j<doc.Entries();++j){const STSEntry e=doc.Entry(int(j));const int id=e.marginRect.left;
            check(id>=0&&id<12&&!seen[id],"Sort original identity bijection");seen[id]=true;
            const STSEntry& old=originals[size_t(id)];
            check(same_identity(e.str,old.str)&&same_identity(e.style,old.style)&&same_identity(e.actor,old.actor)&&same_identity(e.effect,old.effect)
                &&e.marginRect==old.marginRect&&e.fUnicode==old.fUnicode&&e.start==old.start&&e.end==old.end&&e.layer==old.layer&&e.readorder==old.readorder,"Sort preserves complete initialized record");
        }
        printf("sort-stage %s\n",stages[n]);doc.Dump(stages[n]);
        doc.Animate(0); // each subsequent Sort must rebuild, including repeated same comparator
    }
    doc.expected=NULL;
}
static void base_empty_lifecycle() {
    TraceDocument doc(0);
    const wchar_t rawName[]={L'E',0,0xd800,L'N'},rawPath[]={L'P',0,0xdcff,L'X'};
    doc.Name(CString(rawName,4));doc.Path(CString(rawPath,4));doc.Screen(CSize(123,456));
    doc.Wrap(-1);doc.Collisions(1);doc.Scaled(true);doc.SetForceDefaultStyle(true);doc.PAR(1.75);
    STSStyle defaultValue;defaultValue.fontName=CString(rawName,4);defaultValue.fontSize=-0.0;
    check(doc.SetDefaultStyle(defaultValue),"Empty fixture default set");
    const CString name=doc.Name(),path=doc.Path();const MetadataValue metadata=doc.Metadata();
    const int mode=doc.Mode(),wrap=doc.Wrap(),collisions=doc.Collisions();const exttype type=doc.Type();
    const CTextFile::enc encoding=doc.Encoding();const bool scaled=doc.Scaled(),simple=doc.m_simple;
    const LCID lcid=doc.LCIDValue();const CSize layout=doc.LayoutSize();
    named_add(doc,L"EmptyA",named_value(1));named_add(doc,L"EmptyB",named_value(2));
    for(int i=0;i<3;++i){const wchar_t payload[]={wchar_t(L'A'+i),0,wchar_t(0xd800+i),L'Z'};
        doc.AddSTSEntryOnly(CStringW(payload,4),true,i*10,i*10+20,CString(i%2?L"EmptyB":L"EmptyA"));}
    doc.CreateSegments();doc.Animate(0);
    doc.AddSTSEntryOnly(CStringW(rawPath,4),true,60,70,CString(L"EmptyB")); // valid stale animated segments
    check(doc.Segment(0).animated&&doc.Entries()==4,"Empty fixture stale animated state");
    printf("empty-stage empty-populated\n");doc.Dump("empty-populated");
    const unsigned notifications=change_events[0],empties=empty_events[0];
    auto cleared=[&](const char* stage){
        check(doc.Entries()==0&&doc.Segments()==0&&doc.NamedCount()==0&&doc.Screen()==CSize(0,0),"Empty clears only owned collections/screen");
        check(same_identity(doc.Name(),name)&&same_identity(doc.Path(),path)&&same_metadata(doc.Metadata(),metadata)
            &&doc.Mode()==mode&&doc.Type()==type&&doc.Encoding()==encoding&&doc.Wrap()==wrap&&doc.Collisions()==collisions&&doc.Scaled()==scaled
            &&doc.m_simple==simple&&doc.LCIDValue()==lcid&&doc.LayoutSize()==layout,"Empty retains initialized identity/metadata");
        STSStyle actual;check(doc.GetDefaultStyle(actual)&&actual==defaultValue&&same_identity(actual.fontName,defaultValue.fontName),"Empty retains complete default");
        uint64_t bits;std::memcpy(&bits,&actual.fontSize,8);check(bits==UINT64_C(0x8000000000000000),"Empty preserves default signedzero");
        printf("empty-stage %s\n",stage);doc.Dump(stage);
    };
    doc.Empty();cleared("empty-cleared");doc.Empty();cleared("empty-repeated");
    check(empty_events[0]==empties+2&&change_events[0]==notifications,"Repeated virtual Empty exactly twice without OnChanged");
    named_add(doc,L"Reused",named_value(3));
    doc.AddSTSEntryOnly(CStringW(rawName,4),true,0,20,CString(L"Reused"));
    doc.AddSTSEntryOnly(CStringW(rawPath,4),true,10,30,CString(L"Reused"));
    doc.CreateSegments();check(change_events[0]==notifications+1&&doc.Entries()==2&&doc.NamedCount()==1&&doc.Segments()>0,"Empty owner reusable with rebuilt callback");
    printf("empty-stage empty-reused\n");doc.Dump("empty-reused");
    doc.Empty();cleared("empty-final");
    check(empty_events[0]==empties+3&&change_events[0]==notifications+1,"Final Empty no notification");
}
static void lcid_lifecycle() {
    TraceDocument source(0),destination(1);
    source.LCIDValue(1031);destination.LCIDValue(0);
    source.AddSTSEntryOnly(CStringW(L"LCID source"),true,0,10);
    auto stage=[&](const char* name,LCID sourceExpected,LCID destinationExpected){
        check(source.LCIDValue()==sourceExpected&&destination.LCIDValue()==destinationExpected,"supplied LCID retention/independence");
        printf("lcid-stage %s source=%lu destination=%lu\n",name,static_cast<unsigned long>(source.LCIDValue()),static_cast<unsigned long>(destination.LCIDValue()));
        source.Dump(name);destination.Dump(name);
    };
    stage("lcid-supplied",1031,0);
    destination.Copy(source);stage("lcid-copy-retained",1031,0);
    source.LCIDValue(1033);stage("lcid-source-independent",1033,0);
    const unsigned notifications=change_events[1];
    destination.Empty();destination.Empty();stage("lcid-repeated-empty",1033,0);
    check(change_events[1]==notifications,"LCID Empty has no notification");
    destination.LCIDValue(1031);destination.AddSTSEntryOnly(CStringW(L"LCID reuse"),true,0,20);destination.CreateSegments();
    destination.Empty();stage("lcid-reused-empty",1033,1031);
    check(change_events[1]==notifications+1,"LCID reuse exactly one rebuild notification");
}
static void resolution_lifecycle() {
    TraceDocument doc(0);
    STSStyle upper=named_value(1),lower=named_value(2),mapped=named_value(3),empty=named_value(4);
    upper.charSet=SHIFTJIS_CHARSET;lower.charSet=GREEK_CHARSET;mapped.charSet=RUSSIAN_CHARSET;empty.charSet=HANGEUL_CHARSET;
    named_add(doc,L"Case",upper);named_add(doc,L"case",lower);named_add(doc,L"Default",mapped);
    const wchar_t key[]={0,L'K',0xd800},alias[]={0,L'A',0xdcff},prefix[]={L'C',L'a',L's',L'e',0,L'X',0xd800};
    STSStyle* owned=new STSStyle;*owned=empty;doc.AddStyle(CString(key,3),owned);
    doc.AddSTSEntryOnly(CStringW(L"raw prefix resolution"),true,0,10,CString(prefix,7));
    doc.AddSTSEntryOnly(CStringW(L"lowercase resolution"),true,10,20,CString(L"case"));
    doc.AddSTSEntryOnly(CStringW(L"uppercase fallback"),true,20,30,CString(L"CASE"));
    doc.AddSTSEntryOnly(CStringW(L"empty prefix resolution"),true,30,40,CString(alias,3));
    const unsigned notifications=change_events[0];
    auto stage=[&](const char* name,int index,const STSStyle& expected){
        printf("resolution-stage %s\n",name);resolved(doc,index,expected,name);
        STSStyle actual;check(doc.GetStyle(index,&actual)&&same_identity(actual.fontName,expected.fontName),"resolved raw font suffix retained");
        doc.Dump(name);
    };
    stage("resolution-raw-prefix",0,upper);stage("resolution-lowercase",1,lower);
    stage("resolution-case-fallback",2,mapped);stage("resolution-empty-prefix",3,empty);
    STSStyle forced=named_value(5);forced.charSet=ANSI_CHARSET;forced.fontSize=-0.0;
    check(doc.SetDefaultStyle(forced),"resolved forced default set");doc.SetForceDefaultStyle(true);
    stage("resolution-forced",0,forced);
    STSStyle replacement=named_value(6);replacement.charSet=HEBREW_CHARSET;replacement.fontSize=-0.0;
    check(doc.SetDefaultStyle(replacement),"resolved forced default replacement");
    stage("resolution-forced-replaced",3,replacement);
    STSStyle output;check(doc.GetStyle(3,&output),"resolved owned output");
    output.fontName.SetAt(3,L'Q');output.fontName.SetAt(2,L'R');output.fontSize=123.5;output.charSet=THAI_CHARSET;
    printf("resolution-mutated-output ");style(output);
    stage("resolution-output-independent",3,replacement);
    STSStyle reread;check(doc.GetStyle(3,&reread),"resolved independent reread");uint64_t bits;std::memcpy(&bits,&reread.fontSize,8);
    check(bits==UINT64_C(0x8000000000000000),"resolved default signedzero retained");
    check(change_events[0]==notifications,"resolution queries/default replacement do not notify");
}
static void unknown_rows(TraceDocument& doc,const char* stage) {
    printf("unknown-stage %s entries=%zu\n",stage,doc.Entries());
    for(size_t i=0;i<doc.Entries();++i){printf("unknown-row %s %zu",stage,i);units(doc.Entry(int(i)).style);printf("\n");}
    doc.Dump(stage);
}
class UnknownOpenDocument : public TraceDocument {
public:
    bool observing=false;
    unsigned notifications=0;
    UnknownOpenDocument():TraceDocument(0){}
protected:
    void OnChanged() override {
        TraceDocument::OnChanged();
        if(observing){++notifications;
            check(Entries()==1&&Segments()==1&&Mode()==TIME,"Open callback sees committed segments/mode");
            check(same_identity(Entry(0).style,CString(L"UnknownOpen")),"Open callback precedes unknown style rewrite");
            printf("unknown-open-callback mode=%d entries=%zu segments=%zu style",Mode(),Entries(),Segments());units(Entry(0).style);printf("\n");}
    }
};
static void unknown_style_lifecycle() {
    TraceDocument doc(0);STSStyle* known=new STSStyle;known->charSet=ANSI_CHARSET;doc.AddStyle(CString(L"Known"),known);
    const wchar_t* names[]={L"!@dkdfjlakfkjjklezdv^5132ae",L"UnknownA",L"UnknownA",L"Known",L"UnknownA",L"Default",L"UnknownB",L"UnknownC"};
    std::vector<STSEntry> before;
    for(int i=0;i<8;++i){doc.AddSTSEntryOnly(CStringW(L"ordinary unknown style"),true,i*10000000,(i+1)*10000000,CString(names[i]),CString(L"actor"),CString(L"effect"),CRect(1,2,3,4),i,i);before.push_back(doc.Entry(i));}
    doc.CreateSegments();doc.Animate(0);const auto segments=segment_snapshot(doc);
    unknown_rows(doc,"unknown-before");const unsigned callbacks=change_events[0];
    doc.ChangeUnknownStylesToDefault();
    check(change_events[0]==callbacks,"unknown style pass does not notify");
    const wchar_t* expected[]={names[0],L"Default",L"Default",L"Known",L"Default",L"Default",L"Default",L"Default"};
    for(int i=0;i<8;++i){const STSEntry actual=doc.Entry(i),old=before[size_t(i)];
        check(same_identity(actual.style,CString(expected[i])),"unknown style exact resulting name");
        check(same_identity(actual.str,old.str)&&same_identity(actual.actor,old.actor)&&same_identity(actual.effect,old.effect)&&actual.fUnicode==old.fUnicode
            &&actual.marginRect==old.marginRect&&actual.layer==old.layer&&actual.start==old.start&&actual.end==old.end&&actual.readorder==old.readorder,"unknown style preserves other initialized fields");}
    require_same_segments(doc,segments,"unknown-after");unknown_rows(doc,"unknown-after");
    UnknownOpenDocument opened;SavedFiles fixture(L"document-lifecycle-unknown.ass");
    CStringA text=ass_fixture("PlayResX: 640\r\nPlayResY: 360\r\n");
    check(text.Replace(",Default,,",",UnknownOpen,,")==1,"ordinary ASS unknown style fixture");
    {CFile file;check(file.Open(fixture.name,CFile::modeCreate|CFile::modeWrite|CFile::typeBinary)!=FALSE,"write unknown ASS fixture");file.Write(text.GetString(),text.GetLength());}
    FixtureTextFile input(CTextFile::UTF8);check(input.Open(fixture.name),"unknown ASS input");input.SetFixturePath(fixture.name);
    opened.observing=true;check(opened.Open(&input,ANSI_CHARSET,CString(L"unknown ASS")),"ordinary unknown ASS Open");opened.observing=false;
    check(opened.notifications==1,"Open has one OnChanged before warning pass");
    check(same_identity(opened.Entry(0).style,CString(L"Default")),"Open final fallback");unknown_rows(opened,"unknown-open-after");
}

// Finite, representable llround histories only; no frame/time arithmetic on
// no-op fps0 calls. Native callbacks read the fully committed state.
class ConversionDocument : public TraceDocument {
public:
    const char* expected=NULL;
    int expectedMode=TIME;
    std::vector<std::pair<REFERENCE_TIME,REFERENCE_TIME> > expectedTimes;
    unsigned notifications=0;
    ConversionDocument():TraceDocument(0){}
    void Observe(const char* prefix,const char* stage,bool animated) {
        check(Mode()==expectedMode&&Entries()==expectedTimes.size(),"conversion mode/count");
        for(size_t i=0;i<Entries();++i){const STSEntry e=Entry(int(i));
            check(e.start==expectedTimes[i].first&&e.end==expectedTimes[i].second,"conversion rounded endpoints");}
        bool any=false;for(size_t i=0;i<Segments();++i)any=any||Segment(int(i)).animated;
        check(any==animated,"conversion animation transition");
        printf("%s %s mode=%d entries=%zu segments=%zu animated=%d times=",prefix,stage,Mode(),Entries(),Segments(),any);
        if(expectedTimes.empty())printf("-");
        for(size_t i=0;i<expectedTimes.size();++i)printf("%s%lld,%lld",i?";":"",expectedTimes[i].first,expectedTimes[i].second);
        printf("\n");
    }
protected:
    void OnChanged() override {
        TraceDocument::OnChanged();
        if(expected){++notifications;Observe("conversion-callback",expected,false);}
    }
};
static void conversion_lifecycle() {
    ConversionDocument doc;
    const wchar_t raw[]={L'C',0,0xd800,L'X'};
    doc.AddSTSEntryOnly(CStringW(raw,4),true,-1200000,-400000,CString(L"Default"),CString(L"actor"),CString(L"effect"),CRect(1,2,3,4),3,7);
    doc.AddSTSEntryOnly(CStringW(L"positive"),true,400000,1200000,CString(L"Default"),CString(L"actor2"),CString(L"effect2"),CRect(5,6,7,8),4,8);
    std::vector<STSEntry> payloads;for(int i=0;i<2;++i)payloads.push_back(doc.Entry(i));
    doc.expectedTimes={{-1200000,-400000},{400000,1200000}};
    doc.CreateSegments();doc.Animate(0);doc.Observe("conversion-stage","conversion-time-input",true);doc.Dump("conversion-time-input");
    auto transition=[&](const char* stage,bool frame,double fps,const std::vector<std::pair<REFERENCE_TIME,REFERENCE_TIME> >& times,bool real){
        doc.expected=stage;doc.expectedMode=frame?FRAME:TIME;doc.expectedTimes=times;
        const unsigned before=doc.notifications;
        std::vector<STSSegment> oldSegments;
        if(!real)for(size_t i=0;i<doc.Segments();++i)oldSegments.push_back(doc.Segment(int(i)));
        if(frame)doc.ConvertToFrameBased(fps);else doc.ConvertToTimeBased(fps);
        if(!real){check(doc.Segments()==oldSegments.size(),"no-op conversion segment count retained");
            for(size_t i=0;i<oldSegments.size();++i){const STSSegment actual=doc.Segment(int(i)),old=oldSegments[i];
                check(actual.start==old.start&&actual.end==old.end&&actual.animated==old.animated&&actual.subs.GetCount()==old.subs.GetCount(),"no-op conversion complete segment retained");
                for(size_t j=0;j<old.subs.GetCount();++j)check(actual.subs[j]==old.subs[j],"no-op conversion members retained");}}

        check(doc.notifications==before+(real?1:0),"conversion exactly one callback only on transition");
        doc.Observe("conversion-stage",stage,!real);doc.Dump(stage);
        for(int i=0;i<2;++i){const STSEntry e=doc.Entry(i),old=payloads[size_t(i)];
            check(same_identity(e.str,old.str)&&same_identity(e.style,old.style)&&same_identity(e.actor,old.actor)&&same_identity(e.effect,old.effect)
                &&e.fUnicode==old.fUnicode&&e.marginRect==old.marginRect&&e.layer==old.layer&&e.readorder==old.readorder,"conversion preserves full non-time entry payload");}
        doc.expected=NULL;
    };
    transition("conversion-frame-half",true,12.5,{{-2,-1},{1,2}},true);
    doc.Animate(0);transition("conversion-frame-noop-zero",true,0.0,{{-2,-1},{1,2}},false);
    transition("conversion-time-half",false,256.0,{{-78125,-39063},{39063,78125}},true);
    doc.Animate(0);transition("conversion-time-noop-zero",false,0.0,{{-78125,-39063},{39063,78125}},false);
    ConversionDocument empty;
    auto emptyTransition=[&](const char* stage,bool frame,double fps,bool real){
        empty.expected=stage;empty.expectedMode=frame?FRAME:TIME;const unsigned before=empty.notifications;
        if(frame)empty.ConvertToFrameBased(fps);else empty.ConvertToTimeBased(fps);
        check(empty.notifications==before+(real?1:0),"empty conversion callback count");
        empty.Observe("conversion-stage",stage,false);empty.Dump(stage);empty.expected=NULL;
    };
    emptyTransition("conversion-empty-frame",true,12.5,true);
    emptyTransition("conversion-empty-frame-noop",true,0.0,false);
    emptyTransition("conversion-empty-time",false,256.0,true);
}

static void layout_size_lifecycle() {
    TraceDocument source(0),destination(1);
    source.LayoutSize(CSize(320,240));destination.LayoutSize(CSize(800,600));
    source.AddSTSEntryOnly(CStringW(L"layout size source"),true,0,10);
    auto stage=[&](const char* name,const CSize& expectedSource,const CSize& expectedDestination){
        check(source.LayoutSize()==expectedSource&&destination.LayoutSize()==expectedDestination,"layout size retention/independence");
        printf("layout-size-stage %s source=%ld,%ld destination=%ld,%ld\n",name,source.LayoutSize().cx,source.LayoutSize().cy,destination.LayoutSize().cx,destination.LayoutSize().cy);
        source.Dump(name);destination.Dump(name);
    };
    stage("layout-size-supplied",CSize(320,240),CSize(800,600));
    destination.Copy(source);stage("layout-size-copy-retained",CSize(320,240),CSize(800,600));
    source.LayoutSize(CSize(352,288));stage("layout-size-source-independent",CSize(352,288),CSize(800,600));
    destination.Empty();destination.Empty();stage("layout-size-empty-retained",CSize(352,288),CSize(800,600));
    destination.Copy(destination);stage("layout-size-self-copy-retained",CSize(352,288),CSize(800,600));
    auto parse=[&](const char* label,const char* headers,const CSize& expected){
        SavedFiles fixture(L"document-lifecycle-layout-size.ass");const CStringA text=ass_fixture(headers);
        {CFile output;check(output.Open(fixture.name,CFile::modeCreate|CFile::modeWrite|CFile::typeBinary)!=FALSE,"layout size fixture create");output.Write(text.GetString(),text.GetLength());}
        FixtureTextFile input(CTextFile::UTF8);check(input.Open(fixture.name),"layout size fixture input");input.SetFixturePath(fixture.name);
        const unsigned before=change_events[1];check(destination.Open(&input,ANSI_CHARSET,CString(L"layout size fixture")),"ordinary ASS layout size Open");
        check(change_events[1]==before+1&&destination.LastCallbackLayoutSize()==expected,"layout size Open callback parser result");
        printf("layout-size-callback %s width=%ld height=%ld\n",label,expected.cx,expected.cy);
        stage(label,CSize(352,288),expected);
    };
    parse("layout-size-parser-x","LayoutResX: 640\r\n",CSize(640,600));
    parse("layout-size-parser-y","LayoutResY: 360\r\n",CSize(640,360));
    parse("layout-size-parser-repeated","LayoutResY: 480\r\nLayoutResX: 800\r\nLayoutResY: 600\r\nLayoutResX: 720\r\n",CSize(720,600));
    parse("layout-size-parser-nonpositive","LayoutResX: -2\r\nLayoutResY: 0\r\n",CSize(-2,0));
}
int main() {
    try {
        if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
        printf("provenance pointer_bits=%zu\n",sizeof(void*)*8);
        printf("environment ansi=%u oem=%u\n",GetACP(),GetOEMCP());
        default_style_lifecycle();
        copy_selected_lifecycle();
        metadata_lifecycle();
        layout_lifecycle();
        interpretation_lifecycle();
        identity_lifecycle();
        named_style_lifecycle();
        prepared_style_conflicts();
        add_style_session_lifecycle();
        copy_styles_session_lifecycle();
        sort_lifecycle();
        base_empty_lifecycle();
        lcid_lifecycle();
        resolution_lifecycle();
        layout_size_lifecycle();
        conversion_lifecycle();
        unknown_style_lifecycle();
        named_style_exports();
        {
            TraceDocument source(0),destination(1);populate(source);populate(destination);
            unsigned notifications=change_events[1];
            destination.Copy(source);destination.Dump("copied-nonempty");source.Dump("copy-source-unchanged");
            check(change_events[1]==notifications,"Copy does not call OnChanged");
            text_edits(destination);source.Dump("copy-independent-source");
            destination.Sort(false);destination.Dump("sort-normal");
            destination.Sort(true);destination.Dump("sort-readorder");
            destination.ConvertToFrameBased(25.0);destination.Animate(0);destination.Dump("converted-to-frame");
            interpretation_stage(destination,"interpretation-frame",FRAME,EXTSRT,CTextFile::UTF8);
            notifications=change_events[1];destination.ConvertToFrameBased(25.0);destination.Dump("frame-noop");
            interpretation_stage(destination,"interpretation-frame-noop",FRAME,EXTSRT,CTextFile::UTF8);
            check(change_events[1]==notifications,"frame no-op does not notify");
            destination.ConvertToTimeBased(25.0);destination.Animate(0);destination.Dump("converted-to-time");
            interpretation_stage(destination,"interpretation-time",TIME,EXTSRT,CTextFile::UTF8);
            notifications=change_events[1];destination.ConvertToTimeBased(25.0);destination.Dump("time-noop");
            interpretation_stage(destination,"interpretation-time-noop",TIME,EXTSRT,CTextFile::UTF8);
            check(change_events[1]==notifications,"time no-op does not notify");
            destination.RemoveAllEntries();destination.Dump("entries-only-removed");
            check(change_events[1]==notifications,"RemoveAllEntries does not notify");
            destination.CreateSegments();destination.Dump("rebuilt-empty");
            destination.Copy(source);destination.Dump("copy-after-clear");
            notifications=change_events[1];destination.Empty();destination.Empty();destination.Dump("repeated-empty");
            check(change_events[1]==notifications,"Empty does not notify");
            destination.Copy(source);destination.Copy(destination);destination.Dump("self-copy");
            check(destination.Entries()==0&&destination.Segments()==0&&destination.NamedCount()==0,"self-copy reads after Empty");
            check(destination.Screen()==CSize(0,0),"self-copy screen size reset");
            source.Copy(source);source.Dump("source-self-copy");
            populate(source);source.Dump("nonempty-before-destruction");
            printf("before-destruction empty=%u,%u changed=%u,%u\n",empty_events[0],empty_events[1],change_events[0],change_events[1]);
        }
        // Base destruction calls base Empty, not the already-destroyed override.
        printf("after-destruction empty=%u,%u changed=%u,%u\n",empty_events[0],empty_events[1],change_events[0],change_events[1]);
        printf("document-native-lifecycle-v1 complete\n");return 0;
    } catch(const std::exception& error) {
        fprintf(stderr,"document-native-lifecycle-v1 failed: %s\n",error.what());return 2;
    } catch(...) {
        fprintf(stderr,"document-native-lifecycle-v1 failed: native exception\n");return 3;
    }
}
