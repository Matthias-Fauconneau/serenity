#include "core.h"

void*   __dso_handle = (void*) &__dso_handle;
//extern "C" int __cxa_atexit(void (*) (void *), void*, void*) { return 0; }
extern "C" int __aeabi_atexit(void (*) (void *), void*, void*) { return 0; }
//extern "C" void __cxa_pure_virtual() {}

#if __arm__
extern "C" void __aeabi_uidivmod(uint num, uint den) {
    assert_(den!=0);
    uint bit = 1;
    register uint div asm("r0") = 0;
    while(den < num && bit && !(den & (1L << 31))) { den <<= 1; bit <<= 1; }
    while (bit) {
        if(num >= den) { num -= den; div |= bit; }
        else { bit >>= 1; den >>= 1; }
    }
    register unused uint mod asm("r1") = num;
    return; //r0=div, r1=mod
}
extern "C" void __aeabi_uidiv(uint num, uint den) { __aeabi_uidivmod(num,den); }
#endif

#include "memory.h"
void* operator new(uint size) { return allocate_(size); }
void operator delete(void*) { /*TODO*/ }
extern "C" void __aeabi_memset(byte* s, uint n, byte c) { clear(s,n,c); }
#include "array.cc"
//Array_Copy_Compare_Sort_Default(int8)
Array_Copy_Compare_Sort_Default(uint8)
//Array_Copy_Compare_Sort_Default(int16)
Array_Copy_Compare_Sort_Default(uint16)
//Array_Copy_Compare_Sort_Default(int32)
//Array_Copy_Compare_Sort_Default(uint32)
//Array_Copy_Compare_Sort_Default(int64)
//Array_Copy_Compare_Sort_Default(uint64)
//Array_Copy_Compare_Sort_Default(float)
