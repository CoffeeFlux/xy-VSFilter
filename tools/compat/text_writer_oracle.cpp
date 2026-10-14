// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_text_writer.h"
static uint64_t comparisons=0,callbacks=0,interruptions=0,mutations=0,units=0;
static std::vector<wchar_t>* writerInput=NULL;
static bool writerMutate=false,writerMutated=false;
static void writerHook(unsigned kind){
    if(writerMutate && !writerMutated && (kind==56 || kind==57 || kind==58)){
        for(size_t i=0;i+1<writerInput->size();++i)(*writerInput)[i]=L'^';writerMutated=true;
    }
}
static void compare(const std::wstring& input,int encoding,int failure=-1,bool mutate=false){
    microCase=comparisons;microSide=0;microInput=input.data();microLength=static_cast<int>(input.size());
    std::vector<wchar_t> a(input.begin(),input.end());a.push_back(0);auto b=a;
    ReferenceTextWriter::CTextFile ra(encoding);CandidateTextWriter::CTextFile rb(encoding);
    searchFailure=failure;searchMode=0;searchAttempts=0;searchEvents.clear();errno=EDOM;microHook=writerHook;writerMutate=mutate;writerMutated=false;writerInput=&a;bool ia=false,ib=false;
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
    try{ra.WriteString(a.data());}catch(const SearchInterrupted&){ia=true;}
#ifdef _WIN32
    DWORD la=GetLastError();
#endif
    int ea=errno;bool ma=writerMutated;auto events=searchEvents;searchAttempts=0;searchEvents.clear();errno=EDOM;writerMutated=false;writerInput=&b;microSide=1;
#ifdef _WIN32
    SetLastError(0x12345678);
#endif
    try{rb.WriteString(b.data());}catch(const SearchInterrupted&){ib=true;}
#ifdef _WIN32
    DWORD lb=GetLastError();if(la!=lb){fprintf(stderr,"Writer LastError mismatch\n");std::abort();}
#endif
    int eb=errno;bool mb=writerMutated;microHook=NULL;
    if(ia!=ib || ma!=mb || ea!=eb || a!=b || ra.writes!=rb.writes || ra.m_encoding!=rb.m_encoding || events!=searchEvents){
        fprintf(stderr,"Text writer mismatch case=%llu length=%zu encoding=%d failure=%d interruption=%d/%d writes=%zu/%zu errno=%d/%d\n",(unsigned long long)comparisons,input.size(),encoding,failure,ia,ib,ra.writes.size(),rb.writes.size(),ea,eb);std::abort();
    }
    ++comparisons;callbacks+=events.size();interruptions+=ia;mutations+=ma;
    for(const auto& write:ra.writes)units+=write.size()-1;
}
int main(){
    microDiagnostics();
    const wchar_t* pieces[]={L"",L"A",L"\n",L"\r",L"\r\n",L"\n\r",L"\t",L"\x80",L"\xff",L"\x100",L"\xd800",L"\xdc00",L"\xd800\xdc00"};
    const unsigned count=sizeof(pieces)/sizeof(*pieces);
    for(unsigned a=0;a<count;++a)for(unsigned b=0;b<count;++b)for(unsigned c=0;c<count;++c){std::wstring text=pieces[a];text+=pieces[b];text+=pieces[c];for(int e=0;e<5;++e)compare(text,e);}
    for(unsigned unit=0;unit<=0xffff;++unit){std::wstring text=L"A\r\n";text.push_back(static_cast<wchar_t>(unit));text+=L"\nZ";for(int e=0;e<4;++e)compare(text,e);}
    for(unsigned pair=0;pair<4096;++pair){std::wstring text;text.push_back(static_cast<wchar_t>(0xd800+(pair%1024)));text.push_back(static_cast<wchar_t>(0xdc00+((pair*37)%1024)));for(int e=1;e<4;++e)compare(text,e);}
    for(unsigned length:{0u,1u,1023u,1024u,1025u,4095u,4096u,4097u,16384u}){std::wstring text;for(unsigned i=0;i<length;++i)text+=i%7==0?L'\n':L'A';for(int e=0;e<5;++e)compare(text,e);}
    for(int e=0;e<5;++e)for(int failure=-1;failure<4;++failure)for(int mutation=0;mutation<2;++mutation)compare(L"A\r\n\xd800\xdc00\nZ",e,failure,mutation!=0);
    std::wstring nul=L"prefix";nul.push_back(0);nul+=L"\r\n\xd800";for(int e=0;e<5;++e)compare(nul,e);
    if(!writerOwners || !writerLoans || !interruptions || !mutations || !units){fprintf(stderr,"Text writer route controls failed\n");return 1;}
    printf("%llu exact complete TextFile WriteString comparisons passed (%llu written byte/code-unit observations, %llu native callback observation values, %llu injected interruptions / %llu source mutations during native calls, %llu Rust buffer owners / %llu native byte loans; every UTF-16 value, raw and paired surrogates, embedded NUL termination, repeated CR/LF, converter capacity crossings, encoding/default/unknown modes, retained state and errno; actual ATL UTF-8 on Windows, controlled conversion on portable hosts, modeled native write boundaries)\n",(unsigned long long)comparisons,(unsigned long long)units,(unsigned long long)callbacks,(unsigned long long)interruptions,(unsigned long long)mutations,(unsigned long long)writerOwners,(unsigned long long)writerLoans);
}
