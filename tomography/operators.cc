#include "operators.h"

static float reduce(CLKernel& kernel, const CLVolume& A, const CLVolume& B, const int3 origin, int3 size) {
    assert_(A.size == B.size);
    CLBufferF Abuffer (size.x*size.y*size.z, A.name); copy(Abuffer, A, origin, size);
    CLBufferF Bbuffer (size.x*size.y*size.z, B.name); copy(Bbuffer, B, origin, size);
    size_t elementCount = size.z*size.y*size.x;
    size_t blockSize = 128; // threadCount
    assert_(elementCount % blockSize == 0); //FIXME
    size_t blockCount = elementCount / blockSize;
    CLBufferF output (blockCount, "reduce "_+A.name+", "_+B.name);
    kernel.localSpace = blockSize*sizeof(float);
    kernel(blockCount, blockSize, Abuffer.pointer, Bbuffer.pointer, output.pointer, uint(elementCount));
    float blockSums[blockCount];
    output.read(mref<float>(blockSums,blockCount));
    float sum = ::sum(ref<float>(blockSums,blockCount));
    assert_(isNumber(sum), kernel.name);
    return sum;
}

extern bool isIntel;
CL(sum, SSE)  float SSE(const CLVolume& A, const CLVolume& B, const int3 origin, int3 size) {
    size = size?:A.size;
    if(isIntel) return SSE(A.read(VolumeF(size,A.name), origin), B.read(VolumeF(size,B.name), origin)); // reduce fails on Intel
    return reduce(CL::SSE, A, B, origin, size);
}
CL(sum, dotProduct)  float dot(const CLVolume& A, const CLVolume& B, const int3 origin, int3 size) {
    size = size?:A.size;
    if(isIntel) return dot(A.read(VolumeF(size,A.name), origin), B.read(VolumeF(size,B.name), origin)); // reduce fails on Intel
    return reduce(CL::dotProduct, A, B, origin, size);
}
