#include "volume.h"

/// Interleaves bits
static uint64 interleave(uint64 bits, uint offset, uint stride) { uint64 interleavedBits=0; for(uint b=0; bits!=0; bits>>=1, b++) interleavedBits |= (bits&1) << (b*stride+offset); return interleavedBits; }
/// Interleaves 3 coordinates
uint64 zOrder(int3 coordinates) { return interleave(coordinates.x, 0, 3)|interleave(coordinates.y, 1, 3)|interleave(coordinates.z, 2, 3); }
#if ZORDER
/// Generates lookup tables of interleaved bits
static buffer<int32> interleavedLookup(uint size, uint offset, uint stride) { buffer<int32> lookup(size); for(uint i: range(size)) { lookup[i]=interleave(i,offset,stride); } return lookup; }
#endif
/// Pack interleaved bits
static uint pack(uint64 bits, uint offset, uint stride) { uint packedBits=0; bits>>=offset; for(uint b=0; bits!=0; bits>>=stride, b++) packedBits |= (bits&1) << b; return packedBits; }
/// Uninterleaves 2 coordinates
int2 zOrder2(uint64 index) { return int2(pack(index,0,2),pack(index,1,2)); }
/// Uninterleaves 3 coordinates
int3 zOrder3(uint64 index) { return int3(pack(index,0,3),pack(index,1,3),pack(index,2,3)); }

void interleavedLookup(Volume& target) {
    if(target.tiled()) { assert(target.offsetX.size == target.sampleCount.x, target.offsetX.size, target.sampleCount.x); assert(target.offsetY.size == target.sampleCount.y, target.offsetY.size, target.sampleCount.y); assert(target.offsetZ.size == target.sampleCount.z); return; } // FIXME: Generates for largest (but might invalidates offset deltas in CylinderVolume)
#if ZORDER
    target.offsetX = interleavedLookup(target.sampleCount.x,0,3);
    target.offsetY = interleavedLookup(target.sampleCount.y,1,3);
    target.offsetZ = interleavedLookup(target.sampleCount.z,2,3);
#else
    target.offsetX = buffer<int32>(target.sampleCount.x);
    for(uint x: range(target.sampleCount.x)) target.offsetX[x] = x;
    target.offsetY = buffer<int32>(target.sampleCount.y);
    for(uint y: range(target.sampleCount.y)) target.offsetX[y] = y * target.sampleCount.x;
    target.offsetZ = buffer<int32>(target.sampleCount.z);
    for(uint z: range(target.sampleCount.z)) target.offsetX[z] = z * target.sampleCount.y * target.sampleCount.x;
#endif
}

//buffer<int32> Volume::offsetX, Volume::offsetY, Volume::offsetZ;
