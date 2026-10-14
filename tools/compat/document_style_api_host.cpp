// SPDX-License-Identifier: GPL-2.0-or-later
// Windows-only host for a test-instrumented DLL. No native style/MFC objects.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <cstdint>
#include <iostream>
#include <string>
#include <stdexcept>
static void require(bool value,const char* detail){if(!value)throw std::runtime_error(detail);}
struct Module {
    HMODULE value;
    explicit Module(HMODULE v):value(v){}
    ~Module(){if(value)FreeLibrary(value);}
    Module(const Module&)=delete;
    Module& operator=(const Module&)=delete;
};
int wmain(int argc,wchar_t** argv){
    if(argc!=5){std::cerr<<"usage: document-style-api-host DLL CLSID ASS normal|long-name\n";return 2;}
    std::cout<<std::unitbuf<<"provenance pointer_bits="<<sizeof(void*)*8<<'\n';
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    wchar_t key[256]={};
    DWORD length=GetEnvironmentVariableW(L"XY_DOCUMENT_STYLE_REGISTRY_KEY",key,256);
    const std::wstring prefix=L"Software\\xyVSFilterCompatibility\\",path=key;
    if(!length||length>=256||path.compare(0,prefix.size(),prefix)||path.size()!=prefix.size()+36)return 2;
    for(size_t i=prefix.size();i<path.size();++i)
        if(!((path[i]>=L'0'&&path[i]<=L'9')||(path[i]>=L'a'&&path[i]<=L'f')||path[i]==L'-'))return 2;
    HKEY settings=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,key,0,nullptr,REG_OPTION_VOLATILE,KEY_ALL_ACCESS,nullptr,&settings,nullptr)!=ERROR_SUCCESS||
       RegOverridePredefKey(HKEY_CURRENT_USER,settings)!=ERROR_SUCCESS)return 5;
    // Override and handle survive DLL detach. Runner deletes the private key
    // after this process exits, as with com_contract_probe.cpp.
    HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(FAILED(initialized))return 3;
    try {
        require(std::wstring(argv[4])==L"normal"||std::wstring(argv[4])==L"long-name","invalid mode");
        {
            HMODULE value=LoadLibraryExW(argv[1],nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
            require(value!=nullptr,"load test DLL failed");Module module(value);
            using Helper=uint32_t(__cdecl*)(const wchar_t*,const wchar_t*,const wchar_t*);
            auto helper=reinterpret_cast<Helper>(GetProcAddress(value,"xy_document_style_test_v1"));
            require(helper!=nullptr,"missing in-module test helper");
            const uint32_t result=helper(argv[2],argv[3],argv[4]);
            if(result!=0)std::cerr<<"document-style-api helper status="<<result<<'\n';
            require(result==0,"in-module helper failed");
        }
        CoUninitialize();
        std::cout<<"document-style-api-v1 complete\n";return 0;
    } catch(const std::exception& e) {
        std::cerr<<"document-style-api host: "<<e.what()<<'\n';CoUninitialize();return 4;
    }
}
