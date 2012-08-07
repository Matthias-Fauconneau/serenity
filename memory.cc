#include "memory.h"
#include "core.h"
#include "linux.h"

byte* heapStart;
byte* heapEnd;
byte* systemEnd;

void setupHeap() {
    systemEnd = heapEnd = heapStart = (byte*)brk(0);
}

byte* allocate_(uint size) {
    //TODO: allocate from free list
    byte* buffer = heapEnd;
    heapEnd += size;
    if(heapEnd>systemEnd) systemEnd=(byte*)brk((void*)((ptr(heapEnd)&0xFFFFF000)+0x1000)); //round to next page
    return buffer;
}
byte* reallocate_(byte* buffer, int size, int need) {
    if(buffer+size==heapEnd) { if(need>size) allocate_(need-size); else unallocate_(buffer+need,size-need); return buffer; }
    byte* new_buffer = (byte*)allocate_(need);
    copy(new_buffer, (byte*)buffer, size);
    unallocate_(buffer,size);
    return new_buffer;
}
void unallocate_(byte* unused buffer, int unused size) {
    //FIXME: freeing only last allocation
    if(buffer+size==heapEnd) { heapEnd=buffer; systemEnd=(byte*)brk(heapEnd); }
    //TODO: add to free list
}
