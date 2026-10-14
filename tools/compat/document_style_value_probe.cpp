// SPDX-License-Identifier: GPL-2.0-or-later
// Actual initialized STSStyle transport and operator== observations only.
// Compile/link against one selected checkout's native Release libraries; all
// CString/FwRect objects remain in this EXE. No rendering, arbitrary input,
// signaling NaNs, allocation-failure histories, or floating-status claims.
#include <stdafx.h>
#include <STS.h>
#include <array>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <climits>
#include <stdexcept>
#include <exception>

using Bytes = std::vector<uint8_t>;
static void require(bool value,const char* message) {
    if(!value) throw std::runtime_error(message);
}
static void put(Bytes& bytes,size_t offset,uint64_t bits,size_t width) {
    require(offset<=bytes.size() && width<=bytes.size()-offset,"wire bounds");
    for(size_t i=0;i<width;++i)bytes[offset+i]=static_cast<uint8_t>(bits>>(8*i));
}
static std::array<const double*,15> floats(const STSStyle& value) {
    return {{&value.fontSize,&value.outlineWidthX,&value.outlineWidthY,
        &value.shadowDepthX,&value.shadowDepthY,&value.fontScaleX,&value.fontScaleY,
        &value.fontSpacing,&value.fBlur,&value.fGaussianBlur,&value.fontAngleZ,
        &value.fontAngleX,&value.fontAngleY,&value.fontShiftX,&value.fontShiftY}};
}
static std::array<double*,15> floats(STSStyle& value) {
    return {{&value.fontSize,&value.outlineWidthX,&value.outlineWidthY,
        &value.shadowDepthX,&value.shadowDepthY,&value.fontScaleX,&value.fontScaleY,
        &value.fontSpacing,&value.fBlur,&value.fGaussianBlur,&value.fontAngleZ,
        &value.fontAngleX,&value.fontAngleY,&value.fontShiftX,&value.fontShiftY}};
}
static Bytes wire(const STSStyle& value) {
    static_assert(sizeof(double)==8 && sizeof(wchar_t)==2 && sizeof(int)==4,"Windows transport widths");
    const int length=value.fontName.GetLength();
    require(length>=0 && static_cast<size_t>(length)<=(static_cast<size_t>(PTRDIFF_MAX)-192)/2,"font name size");
    Bytes bytes(192+static_cast<size_t>(length)*2,0);
    const int ints[9]={value.charSet,value.fontWeight,value.scrAlignment,value.borderStyle,value.relativeTo,
        static_cast<int>(value.marginRect.get().left),static_cast<int>(value.marginRect.get().top),
        static_cast<int>(value.marginRect.get().right),static_cast<int>(value.marginRect.get().bottom)};
    for(size_t i=0;i<9;++i)put(bytes,i*4,static_cast<uint32_t>(ints[i]),4);
    for(size_t i=0;i<4;++i){put(bytes,36+i*4,static_cast<uint32_t>(value.colors[i]),4);bytes[52+i]=value.alpha[i];}
    bytes[56]=value.fItalic?1:0;bytes[57]=value.fUnderline?1:0;bytes[58]=value.fStrikeOut?1:0;
    put(bytes,64,static_cast<uint32_t>(length),4);
    const auto values=floats(value);
    for(size_t i=0;i<values.size();++i){uint64_t bits;std::memcpy(&bits,values[i],8);put(bytes,72+i*8,bits,8);}
    for(int i=0;i<length;++i)put(bytes,192+static_cast<size_t>(i)*2,static_cast<uint16_t>(value.fontName[i]),2);
    return bytes;
}
static CString raw_name(const std::vector<uint16_t>& units) {
    if(units.empty())return CString();
    std::vector<wchar_t> wide;for(uint16_t unit:units)wide.push_back(static_cast<wchar_t>(unit));
    return CString(wide.data(),static_cast<int>(wide.size()));
}
static STSStyle baseline() {
    STSStyle value; // Actual native constructor/defaults, then explicit controls.
    value.charSet=1;value.fontName=L"TransportBase";value.fontWeight=400;
    value.fItalic=false;value.fUnderline=true;value.fStrikeOut=false;
    value.marginRect=CRect(-1,2,3,4);value.scrAlignment=2;value.borderStyle=1;value.relativeTo=2;
    value.colors[0]=0x12345678;value.colors[1]=0x87654321;value.colors[2]=0;value.colors[3]=0xffffffff;
    value.alpha[0]=0;value.alpha[1]=1;value.alpha[2]=128;value.alpha[3]=255;
    const double controls[15]={20.25,1.25,2.25,3.25,4.25,100.5,101.5,0.25,0.5,0.75,10.25,11.25,12.25,13.25,14.25};
    const auto fields=floats(value);for(size_t i=0;i<fields.size();++i)*fields[i]=controls[i];
    return value;
}
static std::vector<STSStyle> corpus() {
    std::vector<STSStyle> values;
    values.emplace_back(); // Preserve and encode the real native default value.
    const STSStyle base=baseline();values.push_back(base);
    for(size_t field=0;field<5;++field)for(int extreme:{INT_MIN,INT_MAX}) {
        STSStyle value=base;
        int* fields[5]={&value.charSet,&value.fontWeight,&value.scrAlignment,&value.borderStyle,&value.relativeTo};
        *fields[field]=extreme;values.push_back(value);
    }
    for(size_t field=0;field<4;++field)for(LONG extreme:{LONG_MIN,LONG_MAX}) {
        STSStyle value=base;CRect rect(value.marginRect.get());
        LONG* fields[4]={&rect.left,&rect.top,&rect.right,&rect.bottom};*fields[field]=extreme;
        value.marginRect=rect;values.push_back(value);
    }
    for(size_t field=0;field<4;++field){STSStyle value=base;value.colors[field]^=0x80000001u;values.push_back(value);}
    for(size_t field=0;field<4;++field){STSStyle value=base;value.alpha[field]^=0xff;values.push_back(value);}
    for(size_t field=0;field<3;++field){STSStyle value=base;bool* fields[3]={&value.fItalic,&value.fUnderline,&value.fStrikeOut};*fields[field]=!*fields[field];values.push_back(value);}
    // Copy these quiet/non-signaling patterns directly to one native double at
    // a time. No arithmetic or float parser can normalize their stored bits.
    const uint64_t patterns[]={
        0x0000000000000000ULL,0x8000000000000000ULL, // +/- zero
        0x3ff8000000000000ULL,0xc004000000000000ULL, // 1.5, -2.5
        0x0010000000000000ULL, // smallest positive normal
        0x0000000000000001ULL,0x8000000000000001ULL, // +/- smallest subnormal
        0x7ff0000000000000ULL,0xfff0000000000000ULL, // +/- infinity
        0x7ff8000000000042ULL,0xfff8000000001234ULL // quiet NaN payloads
    };
    for(size_t field=0;field<15;++field)for(uint64_t bits:patterns) {
        STSStyle value=base;const auto fields=floats(value);std::memcpy(fields[field],&bits,8);values.push_back(value);
    }
    const std::vector<std::vector<uint16_t>> names={
        {},{0},{0,0xd800},{65},{65,0,66},{65,0,67},{97},{0xd800},
        {0xd800,0,0xdc00},{0xdc00},{0xffff},{0xd800,0xdc00}
    };
    for(const auto& units:names){STSStyle value=base;value.fontName=raw_name(units);values.push_back(value);}
    std::vector<uint16_t> terminated;
    for(int i=0;i<base.fontName.GetLength();++i)terminated.push_back(static_cast<uint16_t>(base.fontName[i]));
    terminated.push_back(0);terminated.push_back(0xdfff);
    STSStyle value=base;value.fontName=raw_name(terminated);values.push_back(value);
    require(values.size()<=256,"fixed corpus limit");
    return values;
}
int main(int argc,char**) {
    if(argc!=1)return 2;
    try {
        if(!AfxWinInit(GetModuleHandle(NULL),NULL,GetCommandLine(),0))throw std::runtime_error("AfxWinInit failed");
        const auto values=corpus();
        printf("provenance pointer_bits=%zu\n",sizeof(void*)*8);
        for(size_t i=0;i<values.size();++i) {
            const auto bytes=wire(values[i]);printf("value %zu ",i);
            for(uint8_t byte:bytes)printf("%02x",static_cast<unsigned>(byte));puts("");
        }
        for(size_t i=0;i<values.size();++i)for(size_t j=0;j<values.size();++j)
            printf("equal %zu %zu %d\n",i,j,values[i]==values[j]?1:0);
        printf("summary values=%zu equalities=%zu\n",values.size(),values.size()*values.size());
        puts("document-style-value-v1 complete");
        return 0;
    } catch(const std::exception& error) {fprintf(stderr,"document-style-value: %s\n",error.what());return 4;}
}
