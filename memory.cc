#include "memory.h"
#include "core.h"
#include "linux.h"

#if NOLIBC
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
byte* reallocate_(byte* buffer, uint size, uint need) {
    if(buffer+size==heapEnd) { if(need>size) allocate_(need-size); else unallocate_(buffer+need,size-need); return buffer; }
    byte* new_buffer = (byte*)allocate_(need);
    copy(new_buffer, (byte*)buffer, size);
    unallocate_(buffer,size);
    return new_buffer;
}
void unallocate_(byte* buffer, uint size) {
    //FIXME: freeing only last allocation
    if(buffer+size==heapEnd) { heapEnd=buffer; /*systemEnd=(byte*)brk(heapEnd);*/ }
    //TODO: add to free list
}
#endif
