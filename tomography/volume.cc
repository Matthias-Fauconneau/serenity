#include "volume.h"

/// Interleaves bits
static uint64 interleave(uint64 bits, uint offset, uint stride=3) { uint64 interleavedBits=0; for(uint b=0; bits!=0; bits>>=1, b++) interleavedBits |= (bits&1) << (b*stride+offset); return interleavedBits; }
/// Interleaves 3 coordinates
uint64 zOrder(int3 coordinates) { return interleave(coordinates.x, 0)|interleave(coordinates.y, 1)|interleave(coordinates.z, 2); }
/// Generates lookup tables of interleaved bits
static buffer<uint64> interleavedLookup(uint size, uint offset, uint stride=3) { buffer<uint64> lookup(size); for(uint i: range(size)) { lookup[i]=interleave(i,offset,stride); } return lookup; }

/// Pack interleaved bits
static uint pack(uint64 bits, uint offset, uint stride=3) { uint packedBits=0; bits>>=offset; for(uint b=0; bits!=0; bits>>=stride, b++) packedBits |= (bits&1) << b; return packedBits; }
/// Uninterleaves 3 coordinates
int3 zOrder(uint64 index) { return int3(pack(index,0),pack(index,1),pack(index,2)); }

void interleavedLookup(Volume& target) {
    if(target.tiled()) return;
    target.offsetX = interleavedLookup(target.sampleCount.x,0);
    target.offsetY = interleavedLookup(target.sampleCount.y,1);
    target.offsetZ = interleavedLookup(target.sampleCount.z,2);
}
