// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef _WIN32
#include <atlconv.h>
#endif
#include "microdvd_shell.h"
#include "xy_rust_writer.h"
#include <cerrno>
#ifndef _WIN32
typedef const wchar_t* LPCWSTR;
enum {CP_UTF8=65001};
#endif
static uint64_t writerOwners=0,writerLoans=0;
namespace XyRust {
class ObservedTextWriter : public TextWriter {
public:
    template<class String> ObservedTextWriter(const String& input,bool big):TextWriter(input,big){++writerOwners;}
    const unsigned char* Data()const{++writerLoans;return TextWriter::Data();}
};
}
static const wchar_t* writerConvertInput(const wchar_t* text,unsigned cp){microObserve(58,text);searchEvents.push_back(cp);return text;}
template<int N> class WriterUtf8 {
#ifdef _WIN32
    ATL::CW2AEX<N> converted;
#else
    std::string converted;
    void scalar(uint32_t c){
        if(c<0x80)converted.push_back(static_cast<char>(c));
        else if(c<0x800){converted.push_back(static_cast<char>(0xc0|(c>>6)));converted.push_back(static_cast<char>(0x80|(c&63)));}
        else if(c<0x10000){converted.push_back(static_cast<char>(0xe0|(c>>12)));converted.push_back(static_cast<char>(0x80|((c>>6)&63)));converted.push_back(static_cast<char>(0x80|(c&63)));}
        else {converted.push_back(static_cast<char>(0xf0|(c>>18)));converted.push_back(static_cast<char>(0x80|((c>>12)&63)));converted.push_back(static_cast<char>(0x80|((c>>6)&63)));converted.push_back(static_cast<char>(0x80|(c&63)));}
    }
#endif
public:
    WriterUtf8(const wchar_t* text,unsigned cp)
#ifdef _WIN32
        :converted(writerConvertInput(text,cp),cp){}
#else
    {
        writerConvertInput(text,cp);
        // Controlled UTF-8 boundary on portable hosts; Windows uses ATL above.
        for(size_t i=0;text[i];++i){uint32_t c=static_cast<uint16_t>(text[i]);
            if(c>=0xd800 && c<=0xdbff){uint32_t next=static_cast<uint16_t>(text[i+1]);if(next>=0xdc00 && next<=0xdfff){c=0x10000+((c-0xd800)<<10)+(next-0xdc00);++i;}else c=0xfffd;}
            else if(c>=0xdc00 && c<=0xdfff)c=0xfffd;
            scalar(c);
        }
    }
#endif
    operator const char*()const{
#ifdef _WIN32
        return static_cast<const char*>(converted);
#else
        return converted.c_str();
#endif
    }
};
struct WriterBase {
    std::vector<std::vector<uint32_t>> writes;
    void WriteString(const wchar_t* text){
        microObserve(56,text);std::vector<uint32_t> record(1,1);
        for(size_t i=0;text[i];++i)record.push_back(static_cast<uint16_t>(text[i]));writes.push_back(record);
    }
    void Write(const unsigned char* bytes,unsigned count){
        microObserve(57,L"");searchEvents.push_back(count);std::vector<uint32_t> record(1,2);
        for(unsigned i=0;i<count;++i)record.push_back(bytes[i]);writes.push_back(record);
    }
    void Write(const char* bytes,unsigned count){Write(reinterpret_cast<const unsigned char*>(bytes),count);}
    void Write(const wchar_t* units,unsigned count){
#ifdef _WIN32
        Write(reinterpret_cast<const unsigned char*>(units),count);
#else
        // Windows wchar_t is two bytes; preserve those bytes on 32-bit-wchar hosts.
        std::vector<unsigned char> bytes;
        for(unsigned i=0;i<count/2;++i){uint16_t unit=static_cast<uint16_t>(units[i]);bytes.push_back(static_cast<unsigned char>(unit));bytes.push_back(static_cast<unsigned char>(unit>>8));}
        Write(bytes.data(),count);
#endif
    }
};
#define CW2AEX WriterUtf8
#define __super WriterBase
