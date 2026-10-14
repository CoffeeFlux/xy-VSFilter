// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "xy_rust_byte_storage.h"
namespace XyRust {
// Bitmap pointers keep the native address modulo 16. Account only logical
// bytes; alignment slack remains unwritten and is never exposed to consumers.
class ShiftedPlaneStorage {
    ByteStorage storage;
    size_t count;
    unsigned shift;
    ShiftedPlaneStorage(const ShiftedPlaneStorage&);
    ShiftedPlaneStorage& operator=(const ShiftedPlaneStorage&);
public:
    ShiftedPlaneStorage():count(0),shift(0){}
    ~ShiftedPlaneStorage(){Reset();}
    bool Allocate(size_t bytes,unsigned offset){
        return AllocateWithPrefix(bytes,offset&15);
    }
    // Some native coefficient cursors form a pointer just before the first
    // logical element. Reserve physical prefix storage without reading it.
    bool AllocateWithPrefix(size_t bytes,unsigned offset){
        Reset();
        // Preserve native zero-sized allocation identity through the fallback.
        if(!bytes || offset>2147483647U || bytes>static_cast<size_t>(2147483647)-offset)return false;
        if(!storage.Allocate(bytes+offset))return false;
        count=bytes;shift=offset;g_xy_malloc_used_size+=count;return true;
    }
    bool Ready()const{return storage.Ready();}
    uint8_t* Data()const{return Ready()?storage.Data()+shift:NULL;}
    void Reset(){
        if(storage.Ready())g_xy_malloc_used_size-=count;
        storage.Reset();count=0;shift=0;
    }
};
}
