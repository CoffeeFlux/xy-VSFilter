// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_csri_filename.h"
#include <string>
static uint64_t histories=0,observations=0,opened=0,rejected=0;
static void seed(){errno=EDOM;std::feclearexcept(FE_ALL_EXCEPT);std::feraiseexcept(FE_DIVBYZERO);
#ifdef _WIN32
SetLastError(0x12345678);
#endif
}
static void effects(){const int error=errno,flags=std::fetestexcept(FE_ALL_EXCEPT);
#ifdef _WIN32
const unsigned long last=GetLastError();
#else
const unsigned long last=0;
#endif
filenameEvents.insert(filenameEvents.end(),{static_cast<uint64_t>(error),static_cast<uint64_t>(flags),last});}
typedef csri_inst* (*OpenFile)(csri_rend*,const char*,csri_openflag*);
typedef void (*CloseFile)(csri_inst*);
static std::vector<uint64_t> execute(OpenFile open,CloseFile close,const std::string& name,bool accepted,bool failed,bool decline,bool count){
    filenameRequire(filenameRegions.empty()&&!filenameInstances&&!filenameLocks&&!filenameRenderers);filenameEvents.clear();filenameOpen=accepted;filenameConversionFailure=failed;filenameDecline=decline;filenameConversions=0;
    csri_rend renderer="ignored renderer";csri_openflag flags;
    for(int repeat=0;repeat<2;++repeat){
        seed();csri_inst* value=open(repeat?&renderer:NULL,name.c_str(),repeat?&flags:NULL);effects();filenameEvents.push_back(value?1:0);filenameRequire(filenameRegions.empty());
        if(value){filenameRequire(value->readorder==0&&value->rts&&value->cs);++opened;}else ++rejected;
        seed();close(value);effects();filenameRequire(!filenameInstances&&!filenameLocks&&!filenameRenderers);
    }
    filenameRequire(filenameConversions==(failed?2U:4U));(void)count;return filenameEvents;
}
int main(){
    std::vector<std::string> names={"", "plain.ass", "C:\\subtitles\\test.ass", "caf\xc3\xa9.ass", "\xe6\x97\xa5\xe6\x9c\xac.ass", "emoji-\xf0\x9f\x8e\xac.ass", "bad-\xff.ass", "tail-\xe2\x82",std::string("nul\0ignored",11)};
    for(size_t length:{1,15,16,31,64,255,1024})names.push_back(std::string(length,'x')+".ass");
    for(const auto& name:names)for(bool accepted:{false,true})for(bool failed:{false,true})for(bool decline:{false,true})for(unsigned reply=0;reply<8;++reply){
        filenameReply.clear();for(unsigned i=0;i<reply*7;++i)filenameReply.push_back(static_cast<uint16_t>(0x20+(i*32749+reply)%65503));filenameReply.push_back(0);
        const auto a=execute(ReferenceFilename::csri_open_file,ReferenceFilename::csri_close,name,accepted,failed,decline,true);
        const auto b=execute(CandidateFilename::csri_open_file,CandidateFilename::csri_close,name,accepted,failed,decline,false);
        if(a!=b){fprintf(stderr,"CSRI filename mismatch case=%llu accepted=%d failure=%d decline=%d reply=%u\n",(unsigned long long)histories,accepted,failed,decline,reply);std::abort();}
        ++histories;observations+=a.size();
    }
    filenameRequire(histories&&opened&&rejected&&filenameBytes&&filenameUnwritten&&filenameOwners&&filenameOwners==filenameLoans&&filenameLoans==filenameReleases&&filenameDeclines);
    printf("%llu exact csri_open_file histories / %llu allocation, conversion, string, lifecycle and error-state observations passed (%llu opened / %llu rejected across both sides / %llu initialized filename bytes / %llu unwritten spare bytes tracked without reads / %llu Rust owners, loans and releases / %llu modeled Rust declines; complete pinned open/close, empty/Unicode/malformed-UTF8/embedded-NUL/long names, ignored renderer and flags, first-conversion failure, exact extra-unit capacity, successful conversion then accepted/rejected renderer open, release order and repeated calls; real Windows MultiByteToWideChar, controlled UTF-16 replies elsewhere, renderer/lock/CString boundaries controlled; object/allocation exceptions, second-conversion failure and actual file IO excluded)\n",(unsigned long long)histories,(unsigned long long)observations,(unsigned long long)opened,(unsigned long long)rejected,(unsigned long long)filenameBytes,(unsigned long long)filenameUnwritten,(unsigned long long)filenameOwners,(unsigned long long)filenameDeclines);
}
