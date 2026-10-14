// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xy_rust_plane.h"
namespace XyRust {
// Output-only bytes, without raster cache accounting. Native writers keep the
// single loan until Reset; unwritten tails retain their uninitialized state.
class ByteStorage {
    void* owner;
    uint8_t* data;
    ByteStorage(const ByteStorage&);
    ByteStorage& operator=(const ByteStorage&);
public:
    ByteStorage():owner(NULL),data(NULL){}
    ~ByteStorage(){Reset();}
    bool Ready()const{return owner!=NULL;}
    uint8_t* Data()const{return data;}
    void Swap(ByteStorage& other){
        // Transfer native handles only; existing byte loans remain valid.
        void* old_owner=owner;owner=other.owner;other.owner=old_owner;
        uint8_t* old_data=data;data=other.data;other.data=old_data;
    }
    bool Allocate(size_t count){
        Reset();
        owner=xy_vsfilter_plane_create(count);
        if(owner)data=xy_vsfilter_plane_loan(owner);
        return Ready();
    }
    void Reset(){
        xy_vsfilter_plane_release(owner);
        owner=NULL;data=NULL;
    }
};
}
