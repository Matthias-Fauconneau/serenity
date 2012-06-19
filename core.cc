void*   __dso_handle = (void*) &__dso_handle;
extern "C" int __cxa_atexit(void (*) (void *), void*, void*) { return 0; }
extern "C" void __cxa_pure_virtual() {}
extern "C" void __cxa_guard_acquire() {}
extern "C" void __cxa_guard_release() {}

typedef unsigned int uint;
uint udivmodsi4(uint num, uint den, bool mod) {
  uint bit = 1;
  uint res = 0;
  while(den < num && bit && !(den & (1L << 31))) { den <<= 1; bit <<= 1; }
  while (bit) {
      if(num >= den) { num -= den; res |= bit; }
      else { bit >>= 1; den >>= 1; }
  }
  return mod ? num : res;
}
extern "C" int __aeabi_uidiv(int n, int d) { return udivmodsi4(n,d,0); }
extern "C" int __umodsi3(int n, int d) { return udivmodsi4(n,d,1); }

#include "memory.h"
void* operator new(uint size) { return allocate_(size); }
void operator delete(void*) { /*TODO*/ }
extern "C" void __aeabi_memset(byte* s, uint n, byte c) { clear(s,n,c); }
#include "array.cc"
Array_Copy_Compare_Sort_Default(int8)
//Array_Copy_Compare_Sort_Default(uint8)
//Array_Copy_Compare_Sort_Default(int16)
Array_Copy_Compare_Sort_Default(uint16)
//Array_Copy_Compare_Sort_Default(int32)
Array_Copy_Compare_Sort_Default(uint32)
//Array_Copy_Compare_Sort_Default(int64)
//Array_Copy_Compare_Sort_Default(uint64)
//Array_Copy_Compare_Sort_Default(float)
