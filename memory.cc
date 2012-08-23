#include "memory.h"
#include "core.h"
#include "linux.h"

#if 0
byte* heapEnd;
byte* systemEnd;
void setupHeap() { systemEnd = heapEnd = (byte*)brk(0); }

byte* allocate_(uint size) {
    //TODO: allocate from free list
    byte* buffer = heapEnd;
    heapEnd += size;
    //if(heapEnd>systemEnd) systemEnd=(byte*)brk((void*)((ptr(heapEnd)&0xFFFFF000)+0x1000)); //round to next page
    if(heapEnd>systemEnd) systemEnd=(byte*)brk((void*)((ptr(heapEnd)&0xFFFF0000)+0x10000)); //round to next 16 page
    return buffer;
}
byte* reallocate_(byte* buffer, int size, int need) {
    if(buffer+size==heapEnd) { if(need>size) allocate_(need-size); else unallocate_(buffer+need,size-need); return buffer; }
    byte* new_buffer = (byte*)allocate_(need);
    copy(new_buffer, (byte*)buffer, size);
    unallocate_(buffer,size);
    return new_buffer;
}
void unallocate_(byte* buffer, int size) {
    //FIXME: freeing only last allocation
    if(buffer+size==heapEnd) { heapEnd=buffer; /*systemEnd=(byte*)brk(heapEnd);*/ }
    //TODO: add to free list
}
#else
void setupHeap() {}
extern "C" byte* malloc(long size);
extern "C" int posix_memalign(byte** buffer, long alignment, long size);
extern "C" byte* realloc(void* buffer, long size);
extern "C" void free(byte* buffer);
static byte* lastFree;
byte* allocate_(uint size) { lastFree=0;
    //return malloc(size); //aligned to 8-byte
    byte* buffer; posix_memalign(&buffer,16,size); return buffer;
}
byte* reallocate_(byte* buffer, int unused size, int need) { lastFree=0; return realloc(buffer,need); }
void unallocate_(byte* buffer, int unused size) { assert_(buffer); assert_(lastFree!=buffer); free(lastFree=buffer); }
#endif
