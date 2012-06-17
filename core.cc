#include "array.cc"
void*   __dso_handle = (void*) &__dso_handle;
extern "C" int __cxa_atexit(void (*) (void *), void*, void*) { return 0; }
extern "C" int __cxa_pure_virtual() { logTrace(); log("pure virtual"); exit(-1); }
extern "C" void __cxa_guard_acquire() {}
extern "C" void __cxa_guard_release() {}
void* operator new(size_t size) { return allocate_(size); }
void operator delete(void*) { /*TODO*/ }
//Array_Copy_Compare_Sort_Default(int8)
Array_Copy_Compare_Sort_Default(uint8)
//Array_Copy_Compare_Sort_Default(int16)
Array_Copy_Compare_Sort_Default(uint16)
//Array_Copy_Compare_Sort_Default(int32)
Array_Copy_Compare_Sort_Default(uint32)
//Array_Copy_Compare_Sort_Default(int64)
//Array_Copy_Compare_Sort_Default(uint64)
//Array_Copy_Compare_Sort_Default(float)
