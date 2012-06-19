#include "memory.h"
#include "linux.h"

byte* heapEnd;
byte* systemEnd;

void setupHeap() {
    systemEnd = heapEnd = (byte*)brk(0);
}

byte* allocate_(int size) {
    //TODO: fill free list first
    byte* buffer = heapEnd;
    heapEnd += size;
    while(heapEnd>systemEnd) systemEnd=(byte*)brk(systemEnd+0x1000);
    return buffer;
}
byte* reallocate_(byte* buffer, int size, int need) {
    if(buffer+size==heapEnd) { allocate_(need-size); return buffer; }
    byte* new_buffer = (byte*)allocate_(need);
    copy(new_buffer, (byte*)buffer, size);
    return new_buffer;
}
void unallocate_(byte* buffer, int size) {
    if(buffer+size==heapEnd) heapEnd=buffer; //FIXME: free only last allocation
    //TODO: free list
}
