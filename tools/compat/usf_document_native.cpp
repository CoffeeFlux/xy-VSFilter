// SPDX-License-Identifier: GPL-2.0-or-later
// Actual selected OpenUSF/CUSFSubtitles and initialized document observer.
#define main unused_document_lifecycle_main
#include "document_native_lifecycle.cpp"
#undef main
#include <STS.cpp>
#include <USFSubtitles.cpp>
#include "legacy_color_names.h"


static void usf_u32(std::vector<BYTE>& bytes,unsigned value) {
    for(unsigned i=0;i<4;++i)bytes.push_back(static_cast<BYTE>(value>>(8*i)));
}
static void usf_string(std::vector<BYTE>& bytes,const CStringW& value) {
    usf_u32(bytes,value.GetLength());
    for(int i=0;i<value.GetLength();++i){bytes.push_back(static_cast<BYTE>(value[i]));bytes.push_back(static_cast<BYTE>(value[i]>>8));}
}
static void usf_font(std::vector<BYTE>& bytes,const fontstyle_t& f) {
    usf_string(bytes,f.face);usf_string(bytes,f.size);
    for(unsigned i=0;i<4;++i)usf_string(bytes,f.color[i]);
    for(const CStringW* v:{&f.weight,&f.italic,&f.underline,&f.alpha,&f.outline,&f.shadow,&f.wrap})usf_string(bytes,*v);
}
static void usf_pal(std::vector<BYTE>& bytes,const posattriblist_t& p) {
    for(const CStringW* v:{&p.alignment,&p.relativeto,&p.horizontal_margin,&p.vertical_margin,&p.rotate[0],&p.rotate[1],&p.rotate[2]})usf_string(bytes,*v);
}
static void usf_state(const CUSFSubtitles& source,unsigned index,unsigned phase,const char* label="usf-state") {
    std::vector<BYTE> bytes;
#ifdef XY_VSFILTER_RUST_USF_STATE
    bytes=source.ParsedState();
#else
    const auto& m=source.metadata;
    for(const CStringW* v:{&m.title,&m.date,&m.comment,&m.author.name,&m.author.email,&m.author.url,&m.language.code,&m.language.text,&m.languageext.code,&m.languageext.text})usf_string(bytes,*v);
    usf_u32(bytes,static_cast<unsigned>(source.styles.GetCount()));
    POSITION pos=source.styles.GetHeadPosition();
    while(pos){const auto& s=source.styles.GetNext(pos);usf_string(bytes,s->name);usf_font(bytes,s->fontstyle);usf_pal(bytes,s->pal);}
    usf_u32(bytes,static_cast<unsigned>(source.effects.GetCount()));pos=source.effects.GetHeadPosition();
    while(pos){const auto& e=source.effects.GetNext(pos);usf_string(bytes,e->name);usf_u32(bytes,static_cast<unsigned>(e->keyframes.GetCount()));POSITION kpos=e->keyframes.GetHeadPosition();
        while(kpos){const auto& k=e->keyframes.GetNext(kpos);usf_string(bytes,k->position);usf_font(bytes,k->fontstyle);usf_pal(bytes,k->pal);}}
    usf_u32(bytes,static_cast<unsigned>(source.texts.GetCount()));pos=source.texts.GetHeadPosition();
    while(pos){const auto& t=source.texts.GetNext(pos);usf_u32(bytes,t->start);usf_u32(bytes,t->stop);usf_string(bytes,t->effect);usf_string(bytes,t->style);usf_string(bytes,t->str);usf_pal(bytes,t->pal);}
#endif
    printf("%s index=%u phase=%u bytes=%zu data=",label,index,phase,bytes.size());
    for(auto byte:bytes)printf("%02x",byte);printf("\n");
}
static bool usf_read(CUSFSubtitles& source,const wchar_t* text) {
    const CStringW encoded=CStringW(L"\xfeff")+text;
    return source.Read(reinterpret_cast<const BYTE*>(static_cast<LPCWSTR>(encoded)),encoded.GetLength()*sizeof(WCHAR));
}
static void usf_reload(unsigned index,const wchar_t* first,const wchar_t* second,bool preload) {
    TraceDocument document(0);document.Screen(CSize(0,0));
    if(preload)document.Add(L"existing",true,0,10000000,L"Default",L"",L"",CRect(0,0,0,0),0);
    CUSFSubtitles parser;
    const bool before=usf_read(parser,first);usf_state(parser,index,0);
    check(parser.ConvertToSTS(document),"USF conversion after initial read failed");usf_state(parser,index,1);document.Dump("usf-before-reload");
    const bool after=usf_read(parser,second);usf_state(parser,index,2);
    check(parser.ConvertToSTS(document),"USF conversion after reload failed");usf_state(parser,index,3);document.Dump("usf-after-reload");
    // A separate destination must observe the same already-mutated source.
    TraceDocument other(0);other.Screen(CSize(0,0));
    check(parser.ConvertToSTS(other),"USF conversion to another document failed");usf_state(parser,index,4);other.Dump("usf-other-document");
    printf("usf-reload index=%u before=%u after=%u\n",index,before?1:0,after?1:0);
    printf("usf-case index=%u outcome=%u position=0\n",index,after?1:0);
}

static void usf_case(unsigned index,const CStringW& input,bool preload,unsigned kind=0) {
    TraceDocument document(0);document.Screen(CSize(0,0));
    if(preload)document.Add(L"existing",true,0,10000000,L"Default",L"",L"",CRect(0,0,0,0),0);
    const CStringW encoded=CStringW(L"\xfeff")+input;
    const auto data=reinterpret_cast<const BYTE*>(static_cast<LPCWSTR>(encoded));
    const size_t size=encoded.GetLength()*sizeof(WCHAR);
    CTextFile file;check(file.OpenMemory(data,size),"USF fixture open failed");
    bool accepted=false;
    if(kind==0)accepted=OpenUSF(&file,document,ANSI_CHARSET);
    else {
        CUSFSubtitles parser;accepted=parser.Read(data,size);
        if(accepted){
            check(parser.ConvertToSTS(document),"USF first conversion failed");
            document.Dump("usf-first-conversion");
            if(kind==2){
                const BYTE invalid[]={0xff,0xfe,'<',0,'x',0};
                check(!parser.Read(invalid,sizeof invalid),"USF invalid reload accepted");
            }
            check(parser.ConvertToSTS(document),"USF repeated conversion failed");
        }
    }
    printf("usf-case index=%u outcome=%u position=%llu\n",index,accepted?1:0,file.GetPosition());
    document.Dump("usf-result");
}

static unsigned usf_scalars() {
    std::vector<CStringW> values;
    const wchar_t* literals[]={L"",L"0",L"1",L"-1",L"+1",L"1x",L"x1",L" 1 ",L"\t-2%",L"%2",L"2%tail",L"2tail%",L"2147483647",L"2147483648",L"-2147483648",L"-2147483649",L"4294967295",L"999999999999999999999",L"-999999999999999999999",L"0x1234",L"#",L"##",L"#123456",L"##123456",L"#80112233",L"#ffffffff",L"#-1",L"#7fffffff",L"#80000000",L"# 0x123456",L"##+1234tail",L"aqua",L"purple",L"fuchsia",L"paleturqoise",L"paleturquoise",L"comflowerblue",L"cornflowerblue",L"transparent",L"red",L"RED",L"Red",L" red",L"red ",L"BottomLeft",L"BottomCenter",L"BottomRight",L"MiddleLeft",L"MiddleCenter",L"MiddleRight",L"TopLeft",L"TopCenter",L"TopRight",L"tOpLeFt",L"top left",L"topcenter ",L"\u0130",L"\uff11",L"\u20031",L"\u00a01",L"\xd800",L"\xdfff",L"\ufffe",L"\uffff"};
    for(auto value:literals)values.push_back(CStringW(value));
    for(auto name:legacy_color_names){CStringW value(name);values.push_back(value);value.MakeUpper();values.push_back(value);}
    const wchar_t* parts[]={L"",L"1",L"-2",L"0",L"x",L"2x",L"2147483647",L"2147483648",L"-2147483649",L" 2",L"+3",L"4%",L"1:2",L"1:2:3",L"1:2:3:4",L"1::2"};
    for(auto a:parts)for(auto b:parts)for(auto sep:{L":",L".",L",",L"::"})values.push_back(CStringW(a)+sep+b);
    for(unsigned u=0;u<256;++u){
        WCHAR raw[]={L'1',static_cast<WCHAR>(u),L'2',L':',L'3'};values.push_back(CStringW(raw,5));
        WCHAR zero[]={L'1',0,static_cast<WCHAR>(u),L':',L'2'};values.push_back(CStringW(zero,5));
    }
    const WCHAR raw[][12]={{L'1',0,L'2',0,L'3',0,L'4',0,L'5'}, {L'1',0,0,L':',L'2'},
        {L'1',L':',0,L'2'}, {0,L'1',L':',L'2'}, {L'2',0,L'%'}, {L'2',L'%',0,L'3'},
        {L'#',L'#',0,L'1'}, {L'r',L'e',L'd',0,L'x'}, {L'T',L'o',L'p',L'L',L'e',L'f',L't',0,L'x'}};
    const int lengths[]={9,5,4,4,3,4,4,5,9};
    for(unsigned i=0;i<countof(raw);++i)values.push_back(CStringW(raw[i],lengths[i]));
    const int widths[]={0,1,-1,100,480,640,2147483647,(-2147483647-1)};
    unsigned index=0;
    for(const auto& value:values){
        for(unsigned kind=1;kind<=4;++kind){
            const unsigned count=kind==3?countof(widths):1;
            for(unsigned i=0;i<count;++i){
                const int argument=kind==3?widths[i]:0;errno=1234;
                const int result=kind==1?TimeToInt(value):kind==2?static_cast<int>(ColorToDWORD(value)):kind==3?TranslateMargin(value,argument):TranslateAlignment(value);
                const int after=errno;
                printf("usf-scalar index=%u kind=%u argument=%d result=%d errno=%d units=%d data=",index++,kind,argument,result,after,value.GetLength());
                for(int n=0;n<value.GetLength();++n)printf("%04x",static_cast<unsigned>(value[n]));printf("\n");
            }
        }
    }
    printf("usf-scalars-complete cases=%u values=%zu\n",index,values.size());return index;
}


static std::vector<BYTE> usf_utf8(const CStringW& input) {
    const int count=WideCharToMultiByte(CP_UTF8,0,input,input.GetLength(),NULL,0,NULL,NULL);
    std::vector<BYTE> out(count);if(count)check(WideCharToMultiByte(CP_UTF8,0,input,input.GetLength(),reinterpret_cast<char*>(out.data()),count,NULL,NULL)==count,"UTF8 fixture failed");return out;
}
static std::vector<std::vector<BYTE>> usf_load_inputs() {
    std::vector<std::vector<BYTE>> all;
    const CStringW xml=L"<USFSubtitles><metadata><title>\u0130\u00e9\uff11</title><language code='en'>English</language></metadata><styles><style name='Default'><fontstyle face='Arial' size='20'/></style></styles><subtitles><subtitle start='1' stop='5'><text>A &amp; B</text></subtitle></subtitles></USFSubtitles>";
    const auto utf8=usf_utf8(xml);all.push_back(utf8);
    std::vector<BYTE> bom={0xef,0xbb,0xbf};bom.insert(bom.end(),utf8.begin(),utf8.end());all.push_back(bom);
    for(bool big:{false,true})for(bool marker:{false,true}){
        std::vector<BYTE> data;if(marker){data.push_back(big?0xfe:0xff);data.push_back(big?0xff:0xfe);}
        for(int i=0;i<xml.GetLength();++i){const unsigned u=xml[i];data.push_back(static_cast<BYTE>(big?u>>8:u));data.push_back(static_cast<BYTE>(big?u:u>>8));}all.push_back(data);
    }
    for(bool big:{false,true}){
        std::vector<BYTE> data=big?std::vector<BYTE>{0,0,0xfe,0xff}:std::vector<BYTE>{0xff,0xfe,0,0};
        for(int i=0;i<xml.GetLength();++i){unsigned u=xml[i];for(unsigned n=0;n<4;++n)data.push_back(static_cast<BYTE>(u>>(8*(big?3-n:n))));}all.push_back(data);
    }
    const char* docs[]={"","<","<wrong/>","<USFSubtitles/>","<usfsubtitles/>","<USFSubtitles><broken>",
        "<?xml version='1.0' encoding='UTF-8'?><USFSubtitles><metadata><title>\xc3\xa9</title></metadata></USFSubtitles>",
        "<?xml version='1.0' encoding='ISO-8859-1'?><USFSubtitles><metadata><title>\xe9</title></metadata></USFSubtitles>",
        "<!DOCTYPE USFSubtitles [<!ENTITY word 'Entity'>]><USFSubtitles><metadata><title>&word;</title></metadata></USFSubtitles>",
        "<USFSubtitles xmlns='urn:local'><metadata><language>Namespace</language></metadata></USFSubtitles>",
        "<x:USFSubtitles xmlns:x='urn:local'><metadata><title>Ignored</title></metadata></x:USFSubtitles>",
        "<USFSubtitles><styles><style name='A' name='B'/></styles></USFSubtitles>"};
    for(auto text:docs)all.push_back(std::vector<BYTE>(text,text+strlen(text)));
    for(size_t count:{size_t(0),size_t(1),size_t(2),size_t(3),size_t(4),size_t(7),size_t(23),utf8.size()-1})all.push_back(std::vector<BYTE>(utf8.begin(),utf8.begin()+count));
    return all;
}
static void usf_load_case(unsigned index,const std::vector<BYTE>& bytes,unsigned mode,unsigned apartment,bool preload) {
    CUSFSubtitles source;TraceDocument document(0);document.Screen(CSize(0,0));
    if(preload){check(usf_read(source,L"<USFSubtitles><metadata><language>Before</language></metadata><styles><style name='Default'><fontstyle wrap='no'/></style></styles><subtitles><subtitle start='1' stop='5'><text alignment='TopLeft'>Previous</text></subtitle></subtitles></USFSubtitles>"),"USF preload failed");check(source.ConvertToSTS(document),"USF preload conversion failed");}
    usf_state(source,index,0,"usf-load-state");
    const wchar_t* path=L"usf-load-input-\u00e9.xml";
    if(mode==1){HANDLE file=CreateFileW(path,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);check(file!=INVALID_HANDLE_VALUE,"USF fixture create failed");DWORD written=0;const BOOL ok=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,NULL);CloseHandle(file);check(ok && written==bytes.size(),"USF fixture write failed");}
    if(apartment!=1){CoUninitialize();if(apartment==2)check(SUCCEEDED(CoInitializeEx(NULL,COINIT_MULTITHREADED)),"MTA setup failed");}
    const bool accepted=mode==0?source.Read(bytes.data(),bytes.size()):mode==1?source.Read(path):mode==2?source.Read(static_cast<LPCTSTR>(NULL)):source.Read(L"missing-usf-load-input.xml");
    if(apartment!=1){if(apartment==2)CoUninitialize();check(SUCCEEDED(CoInitializeEx(NULL,COINIT_APARTMENTTHREADED)),"STA restore failed");}
    if(mode==1)check(DeleteFileW(path)!=FALSE,"USF fixture delete failed");
    usf_state(source,index,1,"usf-load-state");check(source.ConvertToSTS(document),"USF post-load conversion failed");usf_state(source,index,2,"usf-load-state");document.Dump("usf-loaded-document");
    printf("usf-load index=%u mode=%u apartment=%u preload=%u outcome=%u bytes=%zu\n",index,mode,apartment,preload?1:0,accepted?1:0,bytes.size());
}
static void usf_loads() {
    unsigned index=0;const auto inputs=usf_load_inputs();
    for(const auto& input:inputs)for(unsigned mode=0;mode<2;++mode)for(unsigned apartment=0;apartment<3;++apartment)for(unsigned preload=0;preload<2;++preload)usf_load_case(index++,input,mode,apartment,preload!=0);
    for(unsigned mode=2;mode<4;++mode)for(unsigned apartment=0;apartment<3;++apartment)for(unsigned preload=0;preload<2;++preload)usf_load_case(index++,{},mode,apartment,preload!=0);
    const WCHAR raw[][5]={{0},{L'A',0,L'B'},{0,L'I',L'Z'},{0x130,L'I',0,0x3a3},{0xd800,L'A',0xdfff}};const int lengths[]={0,3,3,4,3};
    for(unsigned i=0;i<countof(raw);++i){CComBSTR value(lengths[i],raw[i]);CStringW constructed(value),assigned;assigned=value.m_str;
        const HRESULT hr=value.ToLower();printf("usf-bstr index=%u length=%u constructed=%d assigned=%d lower=%08x data=",i,SysStringLen(value),constructed.GetLength(),assigned.GetLength(),static_cast<unsigned>(hr));
        for(unsigned n=0;n<SysStringLen(value);++n)printf("%04x",static_cast<unsigned>(value[n]));printf("\n");}
    CComSafeArray<BYTE> array;check(SUCCEEDED(array.Create(3)),"array creation failed");CComVariant variant(array.m_psa);
    printf("usf-array vt=%u original_locks=%lu copied_locks=%lu original_features=%u copied_features=%u\n",variant.vt,array.m_psa->cLocks,variant.parray->cLocks,array.m_psa->fFeatures,variant.parray->fFeatures);
#ifdef _ATL_USE_WINAPI_FAMILY_DESKTOP_APP
    printf("usf-msxml-desktop value=1\n");
#else
    printf("usf-msxml-desktop value=0\n");
#endif
    printf("usf-loads-complete cases=%u inputs=%zu\n",index,inputs.size());
}

int main(){try{
    check(AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0)!=FALSE,"MFC initialization failed");
    check(SUCCEEDED(CoInitializeEx(NULL,COINIT_APARTMENTTHREADED)),"COM initialization failed");
    printf("usf-provenance pointer_bits=%zu ansi=%u oem=%u\n",sizeof(void*)*8,GetACP(),GetOEMCP());
    unsigned index=0;
    auto emit=[&](const CStringW& text,unsigned kind=0){for(int preload=0;preload<2;++preload)usf_case(index++,text,preload!=0,kind);};
    const wchar_t* roots[]={L"USFSubtitles",L"usfsubtitles",L"USFSUBTITLES",L"wrong",L"USFSubtitles version='1.0'"};
    const wchar_t* bodies[]={L"",L"<subtitles/>",L"<subtitles><subtitle start='1' stop='2'><text>A</text></subtitle></subtitles>",L"<subtitles><subtitle start='1' duration='2'><text>A</text></subtitle></subtitles>",L"<subtitles><subtitle stop='2'><text>A</text></subtitle></subtitles>",L"<subtitles><subtitle start='1'><text>A</text></subtitle></subtitles>"};
    for(auto root:roots)for(auto body:bodies){CStringW name(root);int space=name.Find(' ');if(space>=0)name=name.Left(space);CStringW text;text.Format(L"<%s>%s</%s>\r\n",root,body,static_cast<LPCWSTR>(name));emit(text);}
    const wchar_t* times[]={L"",L"0",L"1",L"-1",L"1.2",L"1:2",L"1:2.3",L"1:2:3.4",L"1::2",L"abc",L"1x",L"2147483647",L"2147483648",L"4294967296",L"99999999999999999",L".5",L"1,2",L"1:2:3:4:5",L" 2 ",L"-1:-2.-3"};
    for(auto start:times)for(auto stop:times){CStringW text;text.Format(L"<USFSubtitles><subtitles><subtitle start='%s' stop='%s' duration='3'><text>A</text></subtitle></subtitles></USFSubtitles>\r\n",start,stop);emit(text);}
    const wchar_t* fonts[]={L"",L"face='Arial'",L"size='0'",L"size='-1'",L"size='20'",L"weight='normal'",L"weight='bold'",L"weight='lighter'",L"weight='bolder'",L"weight='0'",L"italic='yes' underline='YES'",L"color='#80112233' alpha='50'",L"color='red' back-color='blue' outline-color='#ff00ff' shadow-color='bad'",L"alpha='-1' outline-level='2' shadow-level='3'",L"wrap='no'",L"wrap='auto'",L"wrap='bad'",L"color='##123456' alpha='300'"};
    const wchar_t* positions[]={L"",L"alignment='TopLeft'",L"alignment='bad'",L"relative-to='video'",L"horizontal-margin='10%' vertical-margin='20'",L"horizontal-margin='-5%' vertical-margin='2147483647%'",L"rotate-z='10' rotate-x='-2' rotate-y='3'",L"relative-to='WINDOW' alignment='middlecenter'"};
    for(auto font:fonts)for(auto position:positions){CStringW text;text.Format(L"<USFSubtitles><styles><style name='Default'><fontstyle %s/><position %s/></style></styles><subtitles><subtitle start='1' stop='5'><text %s>A<b>B</b><i>C</i></text></subtitle></subtitles></USFSubtitles>\r\n",font,position,position);emit(text);}
    const wchar_t* inheritance[]={L"",L"<style name='Default'><fontstyle size='20' wrap='no'/></style>",L"<style name='default'><fontstyle size='20' wrap='no'/></style><style name='Default'><fontstyle size='25' wrap='auto'/></style>",L"<style name='Named'><fontstyle color='red'/></style><style name='Default'><fontstyle size='20' wrap='no'/></style>",L"<style name='Named'><fontstyle color='red'/><fontstyle size='25'/></style>",L"<wrapper><style name='Nested'><fontstyle size='25'/></style></wrapper>",L"<style name='Named'><position alignment='TopLeft'/><position rotate-z='30'/></style>",L"<style name='Named'><fontstyle wrap='no'/></style><style name='Named'><fontstyle wrap='auto'/></style>"};
    for(auto styles:inheritance){CStringW text;text.Format(L"<USFSubtitles><metadata><language code='en'>English</language><languageext code='fr'>French</languageext></metadata><styles>%s</styles><subtitles><subtitle start='1' stop='3'><text style='Named' alignment='TopLeft' rotate-z='30'>A</text><text style='named'>B</text><text>C</text></subtitle></subtitles></USFSubtitles>\r\n",styles);for(unsigned kind=0;kind<3;++kind)emit(text,kind);}
    const wchar_t* captions[]={L"A &amp; B &lt; C",L"  A\r\n B  ",L"<b>A<i>B</i>C</b>",L"<font face='Arial' size='20' color='#ff112233' outline-level='2' shadow-level='1'>A</font>",L"<k t='25'>ignored</k>A<br>ignored</br>B",L"<unknown>A</unknown><!--comment--><![CDATA[B]]>",L"\u0130\uff11\u2003",L"<font color='red'><font color='blue'>B</font>R</font>"};
    for(auto caption:captions){CStringW text;text.Format(L"<USFSubtitles><subtitles><subtitle start='1' stop='5'><text>%s</text><karaoke>%s</karaoke></subtitle></subtitles></USFSubtitles>\r\n",caption,caption);emit(text);}
    // 5*6*2 + 20*20*2 + 18*8*2 + 8*3*2 + 8*2 = 1212.

    const wchar_t* reloads[]={
        L"<USFSubtitles><metadata><title>Title</title><date>Date</date><comment>Comment</comment><author><name>Name</name><email>Email</email><url>URL</url></author><language code='en'>English</language><languageext code='fr'>French</languageext></metadata><styles><style name='Named'/><style name='Default'><fontstyle face='Arial' size='20' color='red' wrap='no'/><position horizontal-margin='10%' rotate-z='2'/></style></styles><effects><effect name='fade'><keyframes><keyframe position='50'><fontstyle color='blue'/><position rotate-z='9'/></keyframe></keyframes></effect></effects><subtitles><subtitle start='1' stop='5'><text style='Named' effect='fade' alignment='TopLeft' rotate-z='3'>A<b>B</b></text></subtitle></subtitles></USFSubtitles>",
        L"<USFSubtitles/>",
        L"<wrong><metadata><title>Ignored</title></metadata></wrong>",
        L"<USFSubtitles><broken>",
        L"<USFSubtitles><subtitles><subtitle start='2' duration='3'><text>C<font color='red'>D</font></text></subtitle></subtitles></USFSubtitles>",
        L"<USFSubtitles><metadata><wrapper><title>Outer<title>Inner</title></title><language>Unknown</language></wrapper><author><email/><wrapper><name>Ignored</name></wrapper></author></metadata><effects><effect name='outer'><wrapper><effect name='inner'/><keyframes><keyframe position='0'/><keyframe position='100'/></keyframes></wrapper></effect></effects></USFSubtitles>",
        L"<wrapper><wrong/><USFSubtitles><metadata><language code='ja'>Japanese</language></metadata><subtitles><subtitle start='1.2' stop='2.3'><wrapper><karaoke><k t='25'>Ignored</k>E<br/>F</karaoke></wrapper></subtitle></subtitles></USFSubtitles><USFSubtitles><metadata><language>Ignored</language></metadata></USFSubtitles></wrapper>",
        L"<USFSubtitles><styles><style name='default'><fontstyle size='20' wrap='no'/></style><style name='Default'><fontstyle size='30' wrap='auto'/></style><style name='Named'><wrapper><style name='Renamed'/><fontstyle face='Arial'/><fontstyle color='blue'/></wrapper></style></styles><subtitles><subtitle start='1' stop='5'><text style='Renamed' rotate-x='-2' rotate-y='3'>G<text>H</text></text></subtitle></subtitles></USFSubtitles>"
    };
    for(auto first:reloads)for(auto second:reloads)for(int preload=0;preload<2;++preload)usf_reload(index++,first,second,preload!=0);
    usf_scalars();
    usf_loads();
    printf("usf-complete cases=%u\n",index);CoUninitialize();return 0;
}catch(const std::exception& e){fprintf(stderr,"unresolved: %s\n",e.what());return 1;}catch(...){fprintf(stderr,"unresolved exception\n");return 2;}}
