// SPDX-License-Identifier: GPL-2.0-or-later
// Included once by Rasterizer.cpp. All Rust data crosses as detached bytes.
#include "RustDocumentOwner.h"
extern "C" {
uint32_t __cdecl xy_vsfilter_raster_create_v1(uint64_t,uint64_t,int32_t,int32_t,int32_t,int32_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_raster_read_v1(uint64_t,uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_raster_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_region_create_v1(uint8_t*);
uint32_t __cdecl xy_vsfilter_region_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_region_convert_v1(uint64_t,uint64_t,int32_t,int32_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_region_info_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_region_read_v1(uint64_t,uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_scan_create_v1(uint8_t*);
uint32_t __cdecl xy_vsfilter_scan_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_scan_convert_v1(uint64_t,uint64_t,int32_t,int32_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_scan_info_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_scan_read_v1(uint64_t,uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_path_transform_v1(uint64_t,const uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_path_create_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_path_destroy_v1(uint64_t);
uint32_t __cdecl xy_vsfilter_path_info_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_path_read_v1(uint64_t,uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_path_write_v1(uint64_t,const uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_path_pair_v1(uint64_t,uint64_t,uint32_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_path_action_v1(uint64_t,uint32_t,uintptr_t,int32_t,int32_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_path_text_v1(uint64_t,uintptr_t,const uint8_t*,size_t,uint8_t*);
}
namespace XyRust {
namespace {
uint64_t NewPath(uint64_t source){std::array<uint8_t,8> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_path_create_v1(source,out.data());}));return DocumentDetail::Load(out,0,8);}
std::array<uint8_t,16> PathAction(uint64_t id,uint32_t op,HDC dc=nullptr,long dx=0,long dy=0){std::array<uint8_t,16> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_path_action_v1(id,op,reinterpret_cast<uintptr_t>(dc),dx,dy,out.data());}));return out;}
std::array<uint8_t,12> PathInfo(uint64_t id){std::array<uint8_t,12> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_path_info_v1(id,out.data());}));return out;}
}
RegionOwner::RegionOwner(){std::array<uint8_t,8> out;DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_region_create_v1(out.data());}));id_=DocumentDetail::Load(out,0,8);}
RegionOwner::~RegionOwner(){DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_region_destroy_v1(id_);}));}
std::vector<uint8_t> RegionOwner::Raster(const ScanOwner& scan,int x,int y,int xsub,int ysub)const{
    DocumentDetail::Loan gate;std::array<uint8_t,16> info;
    DocumentDetail::Check(xy_vsfilter_raster_create_v1(scan.id_,id_,x,y,xsub,ysub,info.data()));
    struct ReleaseRaster { uint64_t id;~ReleaseRaster(){DocumentDetail::Release(xy_vsfilter_raster_destroy_v1(id));} } release{DocumentDetail::Load(info,0,8)};
    const size_t length=DocumentDetail::Size(DocumentDetail::Load(info,8,8));DocumentDetail::Shape(length>=32);
    DocumentDetail::Bytes bytes(length);DocumentDetail::Check(xy_vsfilter_raster_read_v1(release.id,bytes.data(),bytes.size()));return bytes;
}
bool RegionOwner::Convert(const ScanOwner& scan,int rx,int ry){std::array<uint8_t,4> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_region_convert_v1(id_,scan.id_,rx,ry,out.data());}));return DocumentDetail::Load(out,0,4)!=0;}
RegionSnapshot RegionOwner::Read()const{
    DocumentDetail::Loan gate;std::array<uint8_t,8> info;DocumentDetail::Check(xy_vsfilter_region_info_v1(id_,info.data()));const uint64_t length=DocumentDetail::Load(info,0,8);
    DocumentDetail::Shape(length>=12&&length<=SIZE_MAX);DocumentDetail::Bytes bytes(static_cast<size_t>(length));DocumentDetail::Check(xy_vsfilter_region_read_v1(id_,bytes.data(),bytes.size()));
    const uint64_t count=DocumentDetail::Load(bytes,4,8);DocumentDetail::Shape(count<=(SIZE_MAX-12)/16&&12+16*count==length);
    RegionSnapshot out;out.mWideBorder=DocumentDetail::I32(DocumentDetail::Load(bytes,0,4));out.mWideOutline.reserve(static_cast<size_t>(count));
    for(size_t i=0;i<count;++i)out.mWideOutline.emplace_back(DocumentDetail::Load(bytes,12+16*i,8),DocumentDetail::Load(bytes,20+16*i,8));return out;
}
ScanOwner::ScanOwner(){std::array<uint8_t,8> out;DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_scan_create_v1(out.data());}));id_=DocumentDetail::Load(out,0,8);}
ScanOwner::~ScanOwner(){DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_scan_destroy_v1(id_);}));}
bool ScanOwner::Convert(const PathOwner& path,int width,int height){std::array<uint8_t,4> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_scan_convert_v1(id_,path.id_,width,height,out.data());}));return DocumentDetail::Load(out,0,4)!=0;}
ScanSnapshot ScanOwner::Read()const{
    DocumentDetail::Loan gate;std::array<uint8_t,8> info;DocumentDetail::Check(xy_vsfilter_scan_info_v1(id_,info.data()));const uint64_t length=DocumentDetail::Load(info,0,8);
    DocumentDetail::Shape(length>=44&&length<=SIZE_MAX);DocumentDetail::Bytes bytes(static_cast<size_t>(length));DocumentDetail::Check(xy_vsfilter_scan_read_v1(id_,bytes.data(),bytes.size()));
    const uint64_t count=DocumentDetail::Load(bytes,36,8);DocumentDetail::Shape(count<=(SIZE_MAX-44)/16&&44+16*count==length);
    ScanSnapshot out;out.mWidth=DocumentDetail::I32(DocumentDetail::Load(bytes,0,4));out.mHeight=DocumentDetail::I32(DocumentDetail::Load(bytes,4,4));
    out.mEdgeNext=static_cast<unsigned>(DocumentDetail::Load(bytes,8,4));out.mEdgeHeapSize=static_cast<unsigned>(DocumentDetail::Load(bytes,12,4));out.fFirstSet=DocumentDetail::Load(bytes,16,4)!=0;
    out.firstp=CPoint(DocumentDetail::I32(DocumentDetail::Load(bytes,20,4)),DocumentDetail::I32(DocumentDetail::Load(bytes,24,4)));out.lastp=CPoint(DocumentDetail::I32(DocumentDetail::Load(bytes,28,4)),DocumentDetail::I32(DocumentDetail::Load(bytes,32,4)));
    out.mOutline.reserve(static_cast<size_t>(count));for(size_t i=0;i<count;++i)out.mOutline.emplace_back(DocumentDetail::Load(bytes,44+16*i,8),DocumentDetail::Load(bytes,52+16*i,8));return out;
}
PathOwner::PathOwner():id_(NewPath(0)){}
PathOwner::PathOwner(const PathOwner& other):id_(NewPath(other.id_)){}
PathOwner::~PathOwner(){DocumentDetail::Release(DocumentDetail::Invoke([&](){return xy_vsfilter_path_destroy_v1(id_);}));}
PathOwner& PathOwner::operator=(const PathOwner& other){std::array<uint8_t,4> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_path_pair_v1(id_,other.id_,1,out.data());}));return *this;}
int PathOwner::Count()const{return static_cast<int>(DocumentDetail::Load(PathInfo(id_),0,8));}
ULONG PathOwner::Hash()const{return static_cast<ULONG>(DocumentDetail::Load(PathInfo(id_),8,4));}
bool PathOwner::Equal(const PathOwner& other)const{std::array<uint8_t,4> out;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_path_pair_v1(id_,other.id_,0,out.data());}));return DocumentDetail::Load(out,0,4)!=0;}
PathSnapshot PathOwner::Read()const{
    DocumentDetail::Loan gate;const size_t count=Count();DocumentDetail::Shape(count<=(SIZE_MAX-4)/9);
    DocumentDetail::Bytes bytes(4+count*9);
    DocumentDetail::Check(xy_vsfilter_path_read_v1(id_,bytes.data(),bytes.size()));
    DocumentDetail::Shape(DocumentDetail::Load(bytes,0,4)==count);PathSnapshot out;out.points.resize(count);out.types.resize(count);
    for(size_t i=0;i<count;++i){out.points[i].x=DocumentDetail::I32(DocumentDetail::Load(bytes,4+9*i,4));out.points[i].y=DocumentDetail::I32(DocumentDetail::Load(bytes,8+9*i,4));out.types[i]=bytes[12+9*i];}return out;
}
void PathOwner::Replace(const PathSnapshot& value){
    const size_t count=value.points.size();DocumentDetail::Shape(count==value.types.size()&&count<=INT_MAX&&count<=(SIZE_MAX-4)/9);
    DocumentDetail::Bytes bytes(4+count*9);DocumentDetail::Put(bytes,0,count,4);
    for(size_t i=0;i<count;++i){DocumentDetail::Put(bytes,4+9*i,static_cast<uint32_t>(value.points[i].x),4);DocumentDetail::Put(bytes,8+9*i,static_cast<uint32_t>(value.points[i].y),4);bytes[12+9*i]=value.types[i];}
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_path_write_v1(id_,bytes.data(),bytes.size());}));
}
void PathOwner::Transform(const uint64_t* values,long x,long y,bool round){
    std::array<uint8_t,81> bytes;for(size_t i=0;i<9;++i)DocumentDetail::Put(bytes,8*i,values[i],8);
    DocumentDetail::Put(bytes,72,static_cast<uint32_t>(x),4);DocumentDetail::Put(bytes,76,static_cast<uint32_t>(y),4);bytes[80]=round?1:0;
    DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_path_transform_v1(id_,bytes.data(),bytes.size());}));
}
void PathOwner::Clear(){PathAction(id_,0);}
bool PathOwner::Text(HDC dc,uintptr_t font,uint64_t spacing,const uint16_t* units,size_t count){
    DocumentDetail::Shape(count<=INT_MAX&&count<=(SIZE_MAX-20)/2);DocumentDetail::Bytes bytes(20+2*count);
    DocumentDetail::Put(bytes,0,font,8);DocumentDetail::Put(bytes,8,spacing,8);DocumentDetail::Put(bytes,16,count,4);
    for(size_t i=0;i<count;++i)DocumentDetail::Put(bytes,20+2*i,units[i],2);
    std::array<uint8_t,4> out;DocumentDetail::Check(DocumentDetail::Invoke([&](){return xy_vsfilter_path_text_v1(id_,reinterpret_cast<uintptr_t>(dc),bytes.data(),bytes.size(),out.data());}));return DocumentDetail::Load(out,0,4)!=0;
}
bool PathOwner::Begin(HDC dc,bool clear){return DocumentDetail::Load(PathAction(id_,1,dc,clear?1:0),0,4)!=0;}
bool PathOwner::End(HDC dc,bool partial,long dx,long dy){return DocumentDetail::Load(PathAction(id_,partial?3:2,dc,dx,dy),0,4)!=0;}
void PathOwner::Align(CPoint* point,CSize* size){const auto out=PathAction(id_,4);
    *point=CPoint(DocumentDetail::I32(DocumentDetail::Load(out,0,4)),DocumentDetail::I32(DocumentDetail::Load(out,4,4)));
    *size=CSize(DocumentDetail::I32(DocumentDetail::Load(out,8,4)),DocumentDetail::I32(DocumentDetail::Load(out,12,4)));}
}
