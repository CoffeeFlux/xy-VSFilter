// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "RustDocumentOwner.h"
#include "xy_rust_shifted_plane.h"
extern "C" {
uint32_t __cdecl xy_vsfilter_gaussian_coefficients_create_v1(uint64_t,uint8_t*);
uint32_t __cdecl xy_vsfilter_gaussian_coefficients_read_v1(uint64_t,uint8_t*,size_t);
uint32_t __cdecl xy_vsfilter_gaussian_coefficients_destroy_v1(uint64_t);
}
namespace XyRust {
// Detached aligned snapshot for the remaining native Gaussian filter. The
// temporary checked coefficient owner is released after the initialized copy.
class GaussianCoefficients {
    ShiftedPlaneStorage storage_;
    static DocumentDetail::Bytes Build(double sigma) {
        using namespace DocumentDetail;
        uint64_t bits;memcpy(&bits,&sigma,8);std::array<uint8_t,16> info;
        Check(Invoke([&](){return xy_vsfilter_gaussian_coefficients_create_v1(bits,info.data());}));
        struct Owner {uint64_t id;~Owner(){Release(Invoke([&](){return xy_vsfilter_gaussian_coefficients_destroy_v1(id);}));}} owner={Load(info,0,8)};
        Bytes out(Size(Load(info,8,8)));Check(Invoke([&](){return xy_vsfilter_gaussian_coefficients_read_v1(owner.id,out.data(),out.size());}));return out;
    }
public:
    int g_r=0,g_w=0,g_w_ex=0;float* g_f=nullptr;double sigma=0;
    explicit GaussianCoefficients(double value) {
        using namespace DocumentDetail;const auto bytes=Build(value);Shape(bytes.size()>=24);
        g_r=I32(Load(bytes,0,4));g_w=I32(Load(bytes,4,4));g_w_ex=I32(Load(bytes,8,4));
        const uint64_t bits=Load(bytes,16,8);memcpy(&sigma,&bits,8);
        if(Load(bytes,12,4)) {
            Shape(g_w_ex>0);const size_t count=MulSize(size_t(g_w_ex),4);Shape(bytes.size()==AddSize(24,count));
            Check(storage_.AllocateWithPrefix(count,16)?0:5);g_f=reinterpret_cast<float*>(storage_.Data());memcpy(g_f,bytes.data()+24,count);
        }else Shape(bytes.size()==24);
    }
    GaussianCoefficients(const GaussianCoefficients& other):g_r(other.g_r),g_w(other.g_w),g_w_ex(other.g_w_ex),sigma(other.sigma) {
        if(g_w_ex>0){const size_t count=DocumentDetail::MulSize(size_t(g_w_ex),4);
            DocumentDetail::Check(storage_.AllocateWithPrefix(count,16)?0:5);g_f=reinterpret_cast<float*>(storage_.Data());memcpy(g_f,other.g_f,count);}
    }
    GaussianCoefficients& operator=(const GaussianCoefficients&)=delete;
};
}
