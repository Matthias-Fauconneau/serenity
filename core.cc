#include "core.h"

#ifdef STANDALONE
extern "C" void _start() { int main(void); exit_(main()); } //TODO: check leaks
void*   __dso_handle = (void*) &__dso_handle;
extern "C" int __cxa_atexit(void (*) (void *), void*, void*) { return 0; }
extern "C" int __aeabi_atexit(void (*) (void *), void*, void*) { return 0; }
extern "C" void __cxa_pure_virtual() { error_("cxa_pure_virtual"); }
void operator delete(void*) { error_("new/delete is deprecated, use alloc<T>(Args...)/free(T*)"); }
extern "C" void memset(byte* dst, uint size, byte value) { clear(dst,size,value); }
extern "C" void memcpy(byte* dst, byte* src, uint size) { copy(dst,src,size); }

#ifdef __arm__
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
extern "C" int __aeabi_uidiv(int num, uint den) { return num<0? -__aeabi_uidivmod(-num,den) : __aeabi_uidivmod(num,den); }
extern "C" int __aeabi_idiv(int num, int den) { return den<0? -__aeabi_uidiv(num,-den) : __aeabi_uidivmod(num,den); }
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
#endif
