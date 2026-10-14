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

class TraceDocument : public CSimpleTextSubtitle {
    unsigned id_;
    CSize initial_screen_,last_callback_screen_;int initial_wrap_,initial_collisions_;bool initial_scaled_;
    int initial_mode_;exttype initial_type_;CTextFile::enc initial_encoding_;
public:
    explicit TraceDocument(unsigned id) : id_(id) {
        initial_screen_=Screen();initial_wrap_=Wrap();initial_collisions_=Collisions();initial_scaled_=Scaled();
        initial_mode_=Mode();initial_type_=Type();initial_encoding_=Encoding();
        // Assign every observed metadata scalar before the first observation;
        // constructor-indeterminate m_simple/m_lcid are never read.
        m_simple = id != 0;Name(id ? CString(L"destination") : CString(L"source"));
        m_lcid = 1033+id;Type(EXTSRT);Mode(TIME);Encoding(CTextFile::UTF8);
        Path(id ? CString(L"destination-path") : CString(L"source-path"));
        Screen(CSize(640+id,480+id));Wrap(int(id));Collisions(int(id));
        Scaled(id!=0);m_layout_size=CSize(800+id,600+id);
        STSStyle initialDefault;initialDefault.charSet=ANSI_CHARSET;initialDefault.fontSize=19+id;
#ifdef XY_VSFILTER_RUST_DOCUMENT_EXPERIMENTAL
        m_document->SetDefaultStyle(initialDefault);
#else
        m_defaultStyle=initialDefault;
#endif
        m_fForcedDefaultStyle=false;m_ePARCompensationType=EPCTDownscale;
        m_dPARCompensation=1.25+id;m_eYCbCrMatrix=YCbCrMatrix_BT709;m_eYCbCrRange=YCbCrRange_PC;
    }
    ~TraceDocument() override {printf("event %u %u destructor-body\n",++sequence,id_);}
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
        printf(" %lu %d %d %d",static_cast<unsigned long>(m_lcid),int(Type()),Mode(),int(Encoding()));units(Path());
        printf(" %ld %ld %d %d %d %ld %ld %d %d",Screen().cx,Screen().cy,
            Wrap(),Collisions(),Scaled(),m_layout_size.cx,m_layout_size.cy,
            m_fForcedDefaultStyle,int(m_ePARCompensationType));
        real(m_dPARCompensation);printf(" %d %d\n",int(m_eYCbCrMatrix),int(m_eYCbCrRange));
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
        last_callback_screen_=Screen();
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
static void named_style_lifecycle() {
    TraceDocument source(0),destination(1);
    unsigned before0=change_events[0],before1=change_events[1];
    const STSStyle first=named_value(1),second=named_value(2),third=named_value(3);
    named_add(source,L"Named",first);named_add(source,L"Named2",second);named_add(source,L"Named3",third);
    source.AddSTSEntryOnly(CStringW(L"rename target"),true,0,10,CString(L"Named"));
    source.AddSTSEntryOnly(CStringW(L"collision stays"),true,10,20,CString(L"Named2"));
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
static void layout_open(const char* label,const char* headers,const CSize& expected,int wrap,int collisions,bool scaled,const CSize& callbackSize) {
    TraceDocument doc(0);SavedFiles fixture(L"document-lifecycle-layout.ass");
    CStringA text("[Script Info]\r\nScriptType: v4.00+\r\n");text+=headers;
    text+="[V4+ Styles]\r\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\r\nStyle: Default,Arial,20,&H00FFFFFF,&H000000FF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,2,2,2,10,10,10,1\r\n[Events]\r\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\r\nDialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,ordinary layout\r\n";
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
int main() {
    try {
        if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
        printf("provenance pointer_bits=%zu\n",sizeof(void*)*8);
        printf("environment ansi=%u oem=%u\n",GetACP(),GetOEMCP());
        default_style_lifecycle();
        layout_lifecycle();
        interpretation_lifecycle();
        identity_lifecycle();
        named_style_lifecycle();
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
