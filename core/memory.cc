#include "memory.h"
#if SIMD
#include "simd.h"
void simdCopy(byte* dst, const byte* src, size_t size) {
    size_t size16 = size/16;
    if(ptr(src)%16==0) {
        if(ptr(dst)%16==0) for(size_t i: range(size16)) *(v16qi*)(dst+i*16) = *(v16qi*)(src+i*16);
        else for(size_t i: range(size16)) storeu(dst+i*16, *(v16qi*)(src+i*16));
    } else {
        if(ptr(dst)%16==0) for(size_t i: range(size16)) *(v16qi*)(dst+i*16) =loadu(src+i*16);
        else {
            if(ptr(dst)%16==ptr(src)%16) {
                size_t align = 16-ptr(dst)%16;
                for(size_t i: range(0, align)) dst[i]=src[i];
                for(size_t i: range(0, size16-16)) *(v16qi*)(dst+align+i*16) = *(v16qi*)(src+align+i*16);
            }
            else error("FIXME"_); //, ptr(dst)%16, ptr(src)%16);
        }
    }
    for(size_t i: range(size16*16,size)) dst[i]=src[i];
}
#endif
