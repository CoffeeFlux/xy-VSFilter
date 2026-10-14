// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only IN-MODULE helper: compile into the corresponding filter DLL.
// All STSStyle/flyweight values remain in that module. No callback-count claim.
#ifdef XY_DOCUMENT_STYLE_TEST_HELPER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "subtitles/stdafx.h"
#include "subtitles/STS.h"
#include "filters/transform/vsfilter/IDirectVobSubXy.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
using namespace DirectVobSubXyOptions;
static void require(bool ok,const char* detail){if(!ok)throw std::runtime_error(detail);}
static void status(const char* label,HRESULT value){std::cout<<label<<" hr="<<std::hex<<static_cast<uint32_t>(value)<<std::dec<<'\n';}
static std::string units(const wchar_t* p,size_t n){std::ostringstream s;s<<n<<':';for(size_t i=0;i<n;++i)s<<static_cast<unsigned>(static_cast<uint16_t>(p[i]))<<',';return s.str();}
// Explicit initialized members only: neither padding nor STSStyle::operator==.
static std::string style(const STSStyle& v){std::ostringstream s;s<<std::hexfloat;
#define FIELD(x) s<<#x<<'='<<v.x<<';'
    FIELD(charSet);s<<"fontName="<<units(v.fontName.GetString(),v.fontName.GetLength())<<';';
    FIELD(fontSize);FIELD(fontWeight);FIELD(fItalic);FIELD(fUnderline);FIELD(fStrikeOut);
    FIELD(marginRect.get().left);FIELD(marginRect.get().top);FIELD(marginRect.get().right);FIELD(marginRect.get().bottom);
    FIELD(scrAlignment);FIELD(borderStyle);FIELD(outlineWidthX);FIELD(outlineWidthY);FIELD(shadowDepthX);FIELD(shadowDepthY);
    for(int i=0;i<4;++i)s<<"color"<<i<<'='<<v.colors[i]<<";alpha"<<i<<'='<<static_cast<unsigned>(v.alpha[i])<<';';
    FIELD(fontScaleX);FIELD(fontScaleY);FIELD(fontSpacing);FIELD(fBlur);FIELD(fGaussianBlur);
    FIELD(fontAngleZ);FIELD(fontAngleX);FIELD(fontAngleY);FIELD(fontShiftX);FIELD(fontShiftY);FIELD(relativeTo);
#undef FIELD
    return s.str();}
static void marker(STSStyle& v,int n){v.SetDefault();v.fontName=L"style-api-sentinel";v.fontSize=120+n;v.fontWeight=500+n;v.colors[0]=0x123400+n;v.marginRect=CRect(70+n,20,20,20);}
struct Rows{std::vector<SubStyle> rows;std::vector<STSStyle> values;explicit Rows(int n):rows(n),values(n){for(int i=0;i<n;++i){for(auto& c:rows[i].name)c=0x5a5a;marker(values[i],i);rows[i].style=&values[i];}} Rows(const Rows&)=delete; Rows& operator=(const Rows&)=delete; Rows(Rows&&)=default; Rows& operator=(Rows&&)=default;};
static std::string row_state(const Rows& a){std::ostringstream s;for(size_t i=0;i<a.rows.size();++i)s<<i<<' '<<units(a.rows[i].name,64)<<' '<<style(a.values[i])<<'\n';return s.str();}
static Rows get(IXyOptions* api,int n,const char* label){Rows r(n);HRESULT h=api->XyGetBin2(BIN2_CUR_STYLES,r.rows.data(),n);status(label,h);require(h==S_OK,"normal getter failed");std::cout<<row_state(r);return r;}
static void names(Rows& dst,const Rows& src){for(size_t i=0;i<dst.rows.size();++i)wcscpy_s(dst.rows[i].name,src.rows[i].name);}
static void normal(IXyOptions* api,int n){require(n>=3,"normal fixture needs at least three styles");Rows baseline=get(api,n,"baseline");
    Rows wrong(n);std::string before=row_state(wrong);HRESULT h=api->XyGetBin2(BIN2_CUR_STYLES,wrong.rows.data(),n-1);status("getter.wrong_count",h);require(h==E_INVALIDARG&&before==row_state(wrong),"wrong getter count changed output");
    h=api->XySetBin(BIN2_CUR_STYLES,baseline.rows.data(),n-1);status("setter.wrong_count",h);require(h==E_INVALIDARG,"wrong setter count accepted");Rows unchanged=get(api,n,"after.wrong_count");require(row_state(unchanged)==row_state(baseline),"wrong setter count changed styles");
    Rows alias(n);STSStyle shared;marker(shared,42);for(auto& r:alias.rows)r.style=&shared;h=api->XyGetBin2(BIN2_CUR_STYLES,alias.rows.data(),n);status("getter.alias",h);require(h==S_OK&&style(shared)==style(baseline.values.back()),"getter alias order differs");for(int i=0;i<n;++i)require(wcscmp(alias.rows[i].name,baseline.rows[i].name)==0,"alias getter names differ");std::cout<<"alias.final "<<style(shared)<<'\n';
    Rows nullable(n);const std::string untouched=style(nullable.values[1]);nullable.rows[1].style=nullptr;h=api->XyGetBin2(BIN2_CUR_STYLES,nullable.rows.data(),n);status("getter.null_style",h);require(h==S_OK&&style(nullable.values[1])==untouched,"null style output changed");for(int i=0;i<n;++i){require(wcscmp(nullable.rows[i].name,baseline.rows[i].name)==0,"null getter name differs");if(i!=1)require(style(nullable.values[i])==style(baseline.values[i]),"null getter other value differs");}std::cout<<row_state(nullable);
    Rows invalid(n);names(invalid,baseline);wcscpy_s(invalid.rows[n-1].name,L"__style_api_missing__");h=api->XySetBin(BIN2_CUR_STYLES,invalid.rows.data(),n);status("setter.missing_late",h);require(h==E_FAIL,"missing setter name accepted");unchanged=get(api,n,"after.missing_late");require(row_state(unchanged)==row_state(baseline),"setter updated before validating all names");
    Rows duplicate(n);names(duplicate,baseline);for(auto& r:duplicate.rows)r.style=nullptr;wcscpy_s(duplicate.rows[1].name,baseline.rows[0].name);duplicate.rows[0].style=&duplicate.values[0];duplicate.rows[1].style=&duplicate.values[1];h=api->XySetBin(BIN2_CUR_STYLES,duplicate.rows.data(),n);status("setter.duplicate",h);require(h==S_OK,"duplicate setter failed");Rows changed=get(api,n,"after.duplicate");require(style(changed.values[0])==style(duplicate.values[1]),"duplicate setter last value lost");for(int i=1;i<n;++i)require(style(changed.values[i])==style(baseline.values[i]),"duplicate setter changed other style");
    Rows nulls(n);names(nulls,changed);for(auto& r:nulls.rows)r.style=nullptr;h=api->XySetBin(BIN2_CUR_STYLES,nulls.rows.data(),n);status("setter.all_null",h);require(h==S_OK,"all-null setter failed");unchanged=get(api,n,"after.all_null");require(row_state(unchanged)==row_state(changed),"all-null setter changed styles");
}
static void long_name(IXyOptions* api,int n){Rows output(n);std::vector<std::string> original;for(auto& v:output.values)original.push_back(style(v));HRESULT h=api->XyGetBin2(BIN2_CUR_STYLES,output.rows.data(),n);status("getter.long_name",h);require(h==static_cast<HRESULT>(ERROR_BUFFER_OVERFLOW),"long-name getter returned unexpected raw status");int prefix=0;while(prefix<n&&output.rows[prefix].name[0]!=0x5a5a){size_t len=0;while(len<64&&output.rows[prefix].name[len])++len;require(len<64,"written name unterminated");require(style(output.values[prefix])!=original[prefix],"prefix style was not written");++prefix;}require(prefix>0&&prefix<n,"long fixture must exercise nonempty partial output");for(int i=prefix;i<n;++i){for(auto c:output.rows[i].name)require(c==0x5a5a,"failure suffix name changed");require(style(output.values[i])==original[i],"failure suffix style changed");}std::cout<<"partial.rows="<<prefix<<'\n'<<row_state(output);}
// Use this module's class factory; no independently linked native style host.
STDAPI DllGetClassObject(REFCLSID, REFIID, LPVOID*);
#ifdef _M_IX86
#pragma comment(linker, "/EXPORT:xy_document_style_test_v1=_xy_document_style_test_v1")
#endif
extern "C" __declspec(dllexport) uint32_t __cdecl xy_document_style_test_v1(
    const wchar_t* clsid_text,const wchar_t* fixture,const wchar_t* mode) {
    std::cout<<std::unitbuf;
    try {
        require(clsid_text&&fixture&&mode,"null helper argument");
        require(std::wstring(mode)==L"normal"||std::wstring(mode)==L"long-name","invalid mode");
        CLSID clsid={};
        require(SUCCEEDED(CLSIDFromString(clsid_text,&clsid)),"invalid CLSID");
        {
            CComPtr<IClassFactory> factory;
            HRESULT h=DllGetClassObject(clsid,IID_IClassFactory,reinterpret_cast<void**>(&factory));
            status("factory",h);require(h==S_OK&&factory,"class factory failed");
            CComPtr<IXyOptions> api;
            h=factory->CreateInstance(nullptr,__uuidof(IXyOptions),reinterpret_cast<void**>(&api));
            status("create",h);require(h==S_OK&&api,"IXyOptions creation failed");
            const size_t length=wcslen(fixture);
            require(length<=INT_MAX,"fixture path too long");
            h=api->XySetString(STRING_FILE_NAME,const_cast<wchar_t*>(fixture),static_cast<int>(length));
            status("load",h);require(h==S_OK,"filename setup failed");
            int n=-1;h=api->XyGetInt(INT_CUR_STYLES_COUNT,&n);
            status("count",h);std::cout<<"styles="<<n<<'\n';
            require(h==S_OK&&n>0&&n<=64,"invalid initial style count");
            if(std::wstring(mode)==L"normal")normal(api,n);else long_name(api,n);
        }
        // Native rows/styles and COM references have all been released here.
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"document-style-api: "<<e.what()<<'\n';return 4;
    }
}
#endif // XY_DOCUMENT_STYLE_TEST_HELPER
