// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "sami_file_shell.h"
#include "xy_rust_pipe_line.h"
#undef swscanf_s
static uint64_t pipeOwners=0,pipeReplies=0;
namespace XyRust {
class ObservedPipeLine : public PipeLine {
public:
    template<class String> explicit ObservedPipeLine(const String& text):PipeLine(text){++pipeOwners;}
    void Found(int found){++pipeReplies;PipeLine::Found(found);}
};
}
template<class... Args> static int pipeScan(const wchar_t* text,const wchar_t* format,Args... args){
    microObserve(54,text);microObserve(55,format);
#ifdef _WIN32
    int result=::swscanf_s(text,format,args...);
#else
    int result=std::swscanf(text,format,args...);
#endif
    searchEvents.push_back(static_cast<uint32_t>(result));
    const int* numbers[]={args...};
    for(int i=0;i<result && i<static_cast<int>(sizeof...(args));++i)searchEvents.push_back(static_cast<uint32_t>(*numbers[i]));
    searchEvents.push_back(errno);return result;
}
#define swscanf_s pipeScan
