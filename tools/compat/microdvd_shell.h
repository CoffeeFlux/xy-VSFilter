// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "find_char_shell.h"
#include "xy_rust_microdvd.h"
#include "xy_rust_mpl2.h"
#include "xy_rust_remove_tags.h"
#include "xy_rust_case_replace.h"
#include <cstring>
#include <algorithm>
#include <csignal>
#include <exception>
#undef CStringW
class MicroString;
static MicroString* activeTrim=NULL;
static uint64_t microCase=0;
static const wchar_t* microInput=NULL;
static int microLength=0,microSide=0;
static void microFailureContext(){
    fprintf(stderr,"MicroDVD fixture context: case=%llu side=%d length=%d attempts=%d units=",(unsigned long long)microCase,microSide,microLength,searchAttempts);
    for(int i=0;i<microLength && i<96;++i)fprintf(stderr,"%04x,",unsigned(microInput[i]));
    fprintf(stderr,"\n");fflush(stderr);
}
static void microDiagnostics(){
    std::signal(SIGABRT,[](int){microFailureContext();std::_Exit(4);});
    std::set_terminate([](){microFailureContext();fprintf(stderr,"Unhandled native exception\n");fflush(stderr);std::_Exit(5);});
#ifdef _WIN32
    _set_invalid_parameter_handler([](const wchar_t* expression,const wchar_t* function,const wchar_t*,unsigned line,uintptr_t){
        microFailureContext();fprintf(stderr,"CRT rejected parameter: function=%ls expression=%ls line=%u\n",function?function:L"?",expression?expression:L"?",line);fflush(stderr);std::_Exit(6);
    });
#endif
}
static uint64_t microSearches=0,microSteps=0,microTagCallbacks=0;
static void (*microHook)(unsigned)=NULL;
static void microObserve(unsigned kind,const wchar_t* text){
    ++microTagCallbacks;searchObserve(kind,0);
    while(*text)searchEvents.push_back(static_cast<uint16_t>(*text++));
    searchEvents.push_back(0x10000);
    if(microHook)microHook(kind);
}
class MicroString {
#ifdef _WIN32
    ATL::CStringW text;
#else
    std::wstring text;
#endif
public:
    MicroString(){}
    MicroString(const wchar_t* p):text(p){}
    MicroString(const wchar_t* p,int n)
#ifdef _WIN32
        :text(p,n){}
#else
        :text(p,static_cast<size_t>(n)){}
#endif
    const wchar_t* GetString()const{
#ifdef _WIN32
        return text.GetString();
#else
        return text.c_str();
#endif
    }
    operator const wchar_t*()const{return GetString();}
    int GetLength()const{
#ifdef _WIN32
        return text.GetLength();
#else
        return static_cast<int>(text.size());
#endif
    }
    wchar_t operator[](int i)const{
#ifdef _WIN32
        return text[i];
#else
        if(i==GetLength())return 0;return text.at(static_cast<size_t>(i));
#endif
    }
    wchar_t GetAt(int i)const{return (*this)[i];}
    MicroString Left(int count)const{
#ifdef _WIN32
        auto result=text.Left(count);return MicroString(result.GetString(),result.GetLength());
#else
        return Mid(0,count);
#endif
    }
    int Find(wchar_t c,int pos=0)const{
#ifdef _WIN32
        return text.Find(c,pos);
#else
        if(pos<0 || pos>=GetLength())return -1;
        const wchar_t* p=std::wcschr(text.c_str()+pos,c);return p?static_cast<int>(p-text.c_str()):-1;
#endif
    }
    int Find(const wchar_t* needle,int pos=0)const{
#ifdef _WIN32
        return text.Find(needle,pos);
#else
        if(pos<0 || pos>=GetLength())return -1;
        const wchar_t* p=std::wcsstr(text.c_str()+pos,needle);return p?static_cast<int>(p-text.c_str()):-1;
#endif
    }
    int Insert(int pos,const wchar_t* value){
#ifdef _WIN32
        return text.Insert(pos,value);
#else
        pos=std::max(0,std::min(pos,GetLength()));text.insert(static_cast<size_t>(pos),value);return GetLength();
#endif
    }
    MicroString Mid(int start)const{
#ifdef _WIN32
        auto result=text.Mid(start);return MicroString(result.GetString(),result.GetLength());
#else
        return Mid(start,GetLength());
#endif
    }
    MicroString Mid(int start,int count)const{
#ifdef _WIN32
        auto result=text.Mid(start,count);return MicroString(result.GetString(),result.GetLength());
#else
        start=std::max(0,std::min(start,GetLength()));count=std::max(0,std::min(count,GetLength()-start));
        return MicroString(text.data()+start,count);
#endif
    }
    bool operator==(const wchar_t* p)const{return std::wcscmp(GetString(),p)==0;}
    MicroString& operator+=(const MicroString& p){text+=p.text;return *this;}
    MicroString& operator+=(const wchar_t* p){text+=p;return *this;}
    MicroString& operator+=(wchar_t p){text+=p;return *this;}
    friend MicroString operator+(const wchar_t* prefix,const MicroString& rest){MicroString result(prefix);result+=rest;return result;}
    int Replace(const wchar_t* from,const wchar_t* to){
#ifdef _WIN32
        return text.Replace(from,to);
#else
        const std::wstring needle(from),replacement(to);size_t pos=0;int count=0;
        while((pos=text.find(needle,pos))!=std::wstring::npos){text.replace(pos,needle.size(),replacement);pos+=replacement.size();++count;}
        return count;
#endif
    }
    int Delete(int start,int count){
#ifdef _WIN32
        return text.Delete(start,count);
#else
        start=std::max(0,std::min(start,GetLength()));count=std::max(0,std::min(count,GetLength()-start));
        text.erase(static_cast<size_t>(start),static_cast<size_t>(count));return GetLength();
#endif
    }
    bool IsEmpty()const{return GetLength()==0;}
    int Remove(wchar_t unit){
#ifdef _WIN32
        return text.Remove(unit);
#else
        const auto old=text.size();text.erase(std::remove(text.begin(),text.end(),unit),text.end());return static_cast<int>(old-text.size());
#endif
    }
    void TrimLeft(){
        MicroString* previous=activeTrim;activeTrim=this;
        try{
            microObserve(9,GetString());
#ifdef _WIN32
            text.TrimLeft();
#else
            size_t i=0;while(i<text.size() && text[i] && std::iswspace(text[i]))++i;text.erase(0,i);
#endif
            microObserve(10,GetString());
        }catch(...){activeTrim=previous;throw;}
        activeTrim=previous;
    }
    void TrimRight(const wchar_t* chars){
        microObserve(11,GetString());
#ifdef _WIN32
        text.TrimRight(chars);
#else
        while(!text.empty() && text.back() && std::wcschr(chars,text.back()))text.pop_back();
#endif
        microObserve(12,GetString());
    }
    MicroString SpanExcluding(const wchar_t* chars)const{
#ifdef _WIN32
        auto result=text.SpanExcluding(chars);return MicroString(result.GetString(),result.GetLength());
#else
        return MicroString(text.data(),static_cast<int>(std::wcscspn(text.c_str(),chars)));
#endif
    }
    friend MicroString operator+(const MicroString& a,const MicroString& b){MicroString result=a;result+=b;return result;}
    friend MicroString operator+(const MicroString& a,const wchar_t* b){MicroString result=a;result+=b;return result;}
    template<class Value> void Format(const wchar_t* fmt,Value x,Value y,Value z){
        microObserve(5,fmt);
#ifdef _WIN32
        text.Format(fmt,x,y,z);
#else
        wchar_t buf[256];std::swprintf(buf,256,fmt,x,y,z);text=buf;
#endif
    }
    void MakeLower(){
        microObserve(4,GetString());
#ifdef _WIN32
        text.MakeLower();
#else
        // CString's in-place lowercase stops at the first NUL.
        for(size_t i=0;i<text.size() && text[i];++i)text[i]=wchar_t(std::towlower(text[i]));
#endif
    }
    void Format(const wchar_t* fmt,const MicroString& input){
        microObserve(5,fmt);
#ifdef _WIN32
        text.Format(fmt,input.GetString());
#else
        text=L"{\\fn";text+=input.GetString();text+=L"}";
#endif
    }
    void Format(const wchar_t* fmt,int value){
        microObserve(5,fmt);
#ifdef _WIN32
        text.Format(fmt,value);
#else
        wchar_t buf[256];std::swprintf(buf,256,fmt,value);text=buf;
#endif
    }
    void Format(const wchar_t* fmt,double value){
        microObserve(5,fmt);
#ifdef _WIN32
        text.Format(fmt,value);
#else
        wchar_t buf[512];std::swprintf(buf,512,fmt,value);text=buf;
#endif
    }
    void Format(const wchar_t* fmt,int x,int y){
        microObserve(5,fmt);
#ifdef _WIN32
        text.Format(fmt,x,y);
#else
        wchar_t buf[256];std::swprintf(buf,256,fmt,x,y);text=buf;
#endif
    }
};
static int microScan(const wchar_t* text,const wchar_t* fmt,int* out){
    microObserve(6,text);
#ifdef _WIN32
    const int result=swscanf_s(text,fmt,out);
#else
    const int result=std::swscanf(text,fmt,out);
#endif
    if(result!=1){fprintf(stderr,"Fixture outside successful numeric-scan scope\n");std::abort();}return result;
}
static int microScan(const wchar_t* text,const wchar_t* fmt,double* out){
    microObserve(6,text);
#ifdef _WIN32
    const int result=swscanf_s(text,fmt,out);
#else
    const int result=std::swscanf(text,fmt,out);
#endif
    if(result!=1){fprintf(stderr,"Fixture outside successful numeric-scan scope\n");std::abort();}return result;
}
static int microScan(const wchar_t* text,const wchar_t* fmt,int* x,wchar_t* c,unsigned count,int* y){
    microObserve(6,text);
#ifdef _WIN32
    const int result=swscanf_s(text,fmt,x,c,count,y);
#else
    (void)fmt;(void)count;const int result=std::swscanf(text,L"{o:%d%lc%d",x,c,y);
#endif
    if(result!=3){fprintf(stderr,"Fixture outside successful move-scan scope\n");std::abort();}return result;
}
static uint64_t replaceHandled=0,replaceFallbacks=0;
static uint64_t removeHandled=0,removeFallbacks=0,removeSteps=0;
static uint64_t mplHandled=0,mplFallbacks=0;
namespace XyRust {
class ObservedCaseReplace : public CaseReplace {
public:
    template<class String> bool Convert(String& source,const String& lowered,const String& from,const String& to){bool result=CaseReplace::Convert(source,lowered,from,to);if(result)++replaceHandled;else ++replaceFallbacks;return result;}
};

class ObservedRemoveTags : public RemoveTags {
public:
    template<class String> explicit ObservedRemoveTags(const String& input):RemoveTags(input){if(Handled())++removeHandled;else ++removeFallbacks;}
    unsigned Step(int reply){++removeSteps;return RemoveTags::Step(reply);}
};

class ObservedMPL2 : public MPL2 {
public:
    template<class String> bool Convert(const String& input,String& output){bool result=MPL2::Convert(input,output);if(result)++mplHandled;else ++mplFallbacks;return result;}
};

class ObservedMicroDVD : public MicroDVD {
public:
    explicit ObservedMicroDVD(int length):MicroDVD(length){++microSearches;}
    unsigned Step(int reply){++microSteps;return MicroDVD::Step(reply);}
};
}
#ifndef _WIN32
#define _wcsnicmp wcsncasecmp
#define _istupper iswupper
typedef wchar_t TCHAR;
#define ZeroMemory(p,n) std::memset(p,0,n)
#endif
#define swscanf_s microScan
#define CStringW MicroString
