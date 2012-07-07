#include "memory.h"
#include "core.h"
#include "linux.h"

byte* heapEnd;
byte* systemEnd;

void setupHeap() {
    systemEnd = heapEnd = (byte*)brk(0);
}

byte* allocate_(int size) {
    //TODO: allocate from free list
    byte* buffer = heapEnd;
    heapEnd += size;
    if(heapEnd>systemEnd) systemEnd=(byte*)brk((void*)((ulong(heapEnd)&0xFFFFF000)+0x1000)); //round to next page
    assert_(systemEnd>=heapEnd);
    return buffer;
}
byte* reallocate_(byte* buffer, int size, int need) {
    if(buffer+size==heapEnd) { allocate_(need-size); return buffer; }
    byte* new_buffer = (byte*)allocate_(need);
    copy(new_buffer, (byte*)buffer, size);
    return new_buffer;
}
void unallocate_(byte* buffer, int size) {
    if(buffer+size==heapEnd) heapEnd=buffer; //FIXME: freeing only last allocation
    //TODO: add to free list
}
