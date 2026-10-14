// SPDX-License-Identifier: GPL-2.0-or-later
// Prepared lifecycle through the private C ABI; no production wrapper claim.
#include "xy_rust_document.h"
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using Bytes = std::vector<uint8_t>;
static void checked(uint32_t status) {
    if(status != XY_DOCUMENT_V1_OK) throw std::runtime_error("document ABI status " + std::to_string(status));
}
static uint64_t load(const Bytes& bytes,size_t offset,size_t width) {
    uint64_t value=0;
    for(size_t i=0;i<width;++i) value |= uint64_t(bytes.at(offset+i)) << (i*8);
    return value;
}
static int64_t signed64(uint64_t value) { int64_t result;std::memcpy(&result,&value,8);return result; }
static int32_t signed32(uint32_t value) { int32_t result;std::memcpy(&result,&value,4);return result; }
static void put(Bytes& bytes,size_t offset,uint64_t value,size_t width) {
    for(size_t i=0;i<width;++i) bytes.at(offset+i)=uint8_t(value>>(i*8));
}
static void append(Bytes& bytes,uint64_t value,size_t width) {
    size_t offset=bytes.size();bytes.resize(offset+width);put(bytes,offset,value,width);
}
struct Owner {
    uint64_t id;
    Owner() { Bytes out(8);checked(xy_vsfilter_document_create_v1(out.data(),out.size()));id=load(out,0,8); }
    Owner(const Owner&)=delete;
    Owner& operator=(const Owner&)=delete;
    ~Owner() { if(xy_vsfilter_document_destroy_v1(id)!=XY_DOCUMENT_V1_OK) std::abort(); }
};
struct Reply {
    uint64_t id;
    Reply(uint64_t owner,uint32_t kind,uint32_t index) {
        Bytes out(8);checked(xy_vsfilter_document_snapshot_open_v1(owner,kind,index,out.data(),out.size()));id=load(out,0,8);
    }
    Reply(const Reply&)=delete;
    ~Reply() { if(xy_vsfilter_document_snapshot_drop_v1(id)!=XY_DOCUMENT_V1_OK) std::abort(); }
    Bytes bytes() const {
        Bytes size(8);checked(xy_vsfilter_document_snapshot_size_v1(id,size.data(),size.size()));
        uint64_t length=load(size,0,8);if(length>SIZE_MAX)throw std::runtime_error("reply size");
        Bytes result(static_cast<size_t>(length));
        checked(xy_vsfilter_document_snapshot_read_v1(id,0,result.data(),result.size()));return result;
    }
};
static Bytes counts(uint64_t owner) {
    Bytes out(8);checked(xy_vsfilter_document_counts_v1(owner,out.data(),out.size()));return out;
}
static Bytes text(int id) {
    Bytes result;
    if(id!=0) for(uint16_t unit : {uint16_t(id),uint16_t(0),uint16_t(0xd800),uint16_t(0xdcff),uint16_t(0xffff)}) append(result,unit,2);
    return result;
}
static Bytes prepared(int id,int64_t start,int64_t end,int order,int layer) {
    Bytes bytes(64),str=text(id);
    put(bytes,0,uint64_t(start),8);put(bytes,8,uint64_t(end),8);
    for(size_t i=0;i<4;++i)put(bytes,16+i*4,uint32_t(id*int(i+1)-7),4);
    put(bytes,32,uint32_t(layer),4);put(bytes,36,uint32_t(order),4);put(bytes,40,id%2!=0,4);
    put(bytes,44,str.size()/2,4);put(bytes,48,1,4);put(bytes,52,2,4);put(bytes,56,2,4);
    bytes.insert(bytes.end(),str.begin(),str.end());append(bytes,uint16_t(id%3+1),2);
    append(bytes,0xdc00,2);append(bytes,uint16_t(id),2);append(bytes,0,2);append(bytes,0xdfff,2);return bytes;
}
static void units(const Bytes& bytes,size_t& offset,size_t length) {
    std::cout<<' '<<length;
    for(size_t i=0;i<length;++i){std::cout<<' '<<load(bytes,offset,2);offset+=2;}
}
static void snapshot(uint64_t owner) {
    Bytes count=counts(owner);uint32_t entries=uint32_t(load(count,0,4)),segments=uint32_t(load(count,4,4));
    std::cout<<entries;
    for(uint32_t i=0;i<entries;++i) {
        Reply reply(owner,0,i);Bytes bytes=reply.bytes();size_t offset=64;
        units(bytes,offset,size_t(load(bytes,44,4)));std::cout<<' '<<load(bytes,40,4);
        for(size_t field=1;field<4;++field)units(bytes,offset,size_t(load(bytes,44+4*field,4)));
        if(offset!=bytes.size())throw std::runtime_error("entry wire length");
        for(size_t j=0;j<4;++j)std::cout<<' '<<signed32(uint32_t(load(bytes,16+j*4,4)));
        std::cout<<' '<<signed32(uint32_t(load(bytes,32,4)))<<' '<<signed64(load(bytes,0,8))<<' '<<signed64(load(bytes,8,8))<<' '<<signed32(uint32_t(load(bytes,36,4)));
    }
    std::cout<<' '<<segments;
    for(uint32_t i=0;i<segments;++i) {
        Reply reply(owner,1,i);Bytes bytes=reply.bytes();size_t members=size_t(load(bytes,20,4));
        std::cout<<' '<<signed64(load(bytes,0,8))<<' '<<signed64(load(bytes,8,8))<<' '<<load(bytes,16,4)<<' '<<members;
        for(size_t j=0;j<members;++j)std::cout<<' '<<load(bytes,32+j*4,4);
        if(bytes.size()!=32+members*4)throw std::runtime_error("segment wire length");
    }
    std::cout<<'\n';
}
int main() {
    try {
        Owner owner,saved;std::string line;
        while(std::getline(std::cin,line)) {
            std::istringstream input(line);char op;if(!(input>>op))return 2;
            if(op=='A') {
                int mode=0,id=0,order=0;int64_t start=0,end=0;if(!(input>>mode>>id>>start>>end>>order))return 2;
                int layer=id-3;if(!(input>>layer)){if(!input.eof())return 2;input.clear();layer=id-3;}
                Bytes bytes=prepared(id,start,end,order,layer),out(8);
                checked(xy_vsfilter_document_add_prepared_v1(owner.id,mode,bytes.data(),bytes.size(),out.data(),out.size()));
            } else if(op=='Q') {
                uint32_t mode=0;int64_t time=0;if(!(input>>mode>>time))return 2;Bytes out(16);
                checked(xy_vsfilter_document_query_time_v1(owner.id,mode,time,out.data(),out.size()));
                std::cout<<"Q "<<signed32(uint32_t(load(out,0,4)))<<' '<<load(out,4,4)<<' '<<load(out,8,4)<<' ';
            } else if(op=='E') {
                uint32_t index=0;int id=0,flag=0;if(!(input>>index>>id>>flag))return 2;Bytes value=text(id);
                checked(xy_vsfilter_document_set_prepared_text_v1(owner.id,index,flag,value.data(),value.size()));
            } else if(op=='O' || op=='T') {
                size_t count=0;if(!(input>>count)||count>4096)return 2;Bytes bytes;
                for(size_t i=0;i<count;++i) {
                    if(op=='O'){uint32_t index=0;if(!(input>>index))return 2;append(bytes,index,4);}
                    else {int64_t start=0,end=0;if(!(input>>start>>end))return 2;append(bytes,uint64_t(start),8);append(bytes,uint64_t(end),8);}
                }
                checked(op=='O'?xy_vsfilter_document_reorder_entries_v1(owner.id,bytes.data(),bytes.size()):xy_vsfilter_document_replace_entry_times_v1(owner.id,bytes.data(),bytes.size()));
            } else if(op=='N') {
                uint16_t old=0,replacement=0;if(!(input>>old>>replacement))return 2;Bytes count=counts(owner.id),value;append(value,replacement,2);
                // The established corpus uses only one-unit non-NUL style names.
                for(uint32_t i=0;i<load(count,0,4);++i) {
                    Reply reply(owner.id,0,i);Bytes bytes=reply.bytes();size_t offset=64+size_t(load(bytes,44,4))*2;
                    if(load(bytes,48,4)==1 && load(bytes,offset,2)==old)
                        checked(xy_vsfilter_document_set_prepared_style_v1(owner.id,i,value.data(),value.size()));
                }
            } else if(op=='F') {
                uint32_t index=0,animated=0;if(!(input>>index>>animated))return 2;checked(xy_vsfilter_document_animation_or_v1(owner.id,index,animated));
            } else if(op=='L') {
                int64_t bound=0;if(!(input>>bound))return 2;Bytes out(16);checked(xy_vsfilter_document_correct_last_end_v1(owner.id,bound,out.data(),out.size()));
            } else if(op=='K') checked(xy_vsfilter_document_copy_storage_v1(saved.id,owner.id));
            else if(op=='B') checked(xy_vsfilter_document_copy_storage_v1(owner.id,saved.id));
            else if(op=='R') checked(xy_vsfilter_document_rebuild_segments_v1(owner.id));
            else if(op=='C') checked(xy_vsfilter_document_clear_storage_v1(owner.id));
            else if(op=='D') checked(xy_vsfilter_document_remove_entries_v1(owner.id));
            else if(op!='P') return 2;
            if(!input)return 2;snapshot(owner.id);
        }
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 2;}
}
