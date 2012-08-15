#include "core.h"

//void*   __dso_handle = (void*) &__dso_handle;
extern "C" int __cxa_atexit(void (*) (void *), void*, void*) { return 0; }
extern "C" int __aeabi_atexit(void (*) (void *), void*, void*) { return 0; }
extern "C" void __cxa_pure_virtual() { trace(0,-1); log_("pure virtual"); abort(); }
void operator delete(void*) { log_("new/delete is deprecated, use alloc<T>(Args...)/free(T*)"); abort(); }
extern "C" void memset(byte* dst, uint size, byte value) { clear(dst,size,value); }
extern "C" void memcpy(byte* dst, byte* src, uint size) { copy(dst,src,size); }

#if __arm__
extern "C" void __aeabi_memset(byte* dst, uint size, byte value) { return memset(dst,size,value); }
extern "C" void __aeabi_memcpy(byte* dst, byte* src, uint size) { memcpy(dst,src,size); }
extern "C" uint __aeabi_uidivmod(uint num, uint den) {
    assert_(den!=0);
    uint bit = 1;
    uint div = 0;
    while(den < num && bit && !(den & (1L << 31))) { den <<= 1; bit <<= 1; }
    while (bit) {
        if(num >= den) { num -= den; div |= bit; }
        else { bit >>= 1; den >>= 1; }
    }
    register unused uint mod asm("r1") = num; //r1=mod
    return div; //r0=div
}
extern "C" int __aeabi_uidiv(int num, uint den) {
    if(den==2) return num>>1;
    if(num<0) return -__aeabi_uidivmod(-num,den);
    return __aeabi_uidivmod(num,den);
}
extern "C" uint __umodsi3(uint num, uint den) {
    assert_(den!=0);
    uint bit = 1;
    uint div = 0;
    while(den < num && bit && !(den & (1L << 31))) { den <<= 1; bit <<= 1; }
    while (bit) {
        if(num >= den) { num -= den; div |= bit; }
        else { bit >>= 1; den >>= 1; }
    }
    return num;
}
#endif
