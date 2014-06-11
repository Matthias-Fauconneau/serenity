#include "volume.h"

static float reduce1(CLKernel& kernel, const CLVolume& A) {
    CLBufferF buffer (A.size.x*A.size.y*A.size.z); copy(buffer, A);
    size_t elementCount = A.size.z*A.size.y*A.size.x;
    size_t blockSize = 128; // threadCount
    assert_(elementCount % blockSize == 0); //FIXME
    size_t blockCount = elementCount / blockSize;
    CLBufferF output (blockCount);
    kernel.localSpace = blockSize*sizeof(float);
    kernel(blockCount, blockSize, buffer.pointer, output.pointer, elementCount);
    float blockSums[blockCount];
    output.read(mref<float>(blockSums,blockCount));
    float sum = ::sum(ref<float>(blockSums,blockCount));
    assert_(isNumber(sum), kernel.name);
    return sum;
}

CL(sum, sum) float sum(const CLVolume& A) { return reduce1(CL::sum, A); }
CL(sum, SSQ) float SSQ(const CLVolume& A) { return reduce1(CL::SSQ, A); }

static float reduce2(CLKernel& kernel, const CLVolume& A, const CLVolume& B) {
    assert_(A.size == B.size);
    CLBufferF Abuffer (A.size.x*A.size.y*A.size.z); copy(Abuffer, A);
    CLBufferF Bbuffer (B.size.x*B.size.y*B.size.z); copy(Bbuffer, B);
    size_t elementCount = A.size.z*A.size.y*A.size.x;
    size_t blockSize = 128; // threadCount
    assert_(elementCount % blockSize == 0); //FIXME
    size_t blockCount = elementCount / blockSize;
    CLBufferF output (blockCount);
    kernel.localSpace = blockSize*sizeof(float);
    kernel(blockCount, blockSize, Abuffer.pointer, Bbuffer.pointer, output.pointer, elementCount);
    float blockSums[blockCount];
    output.read(mref<float>(blockSums,blockCount));
    float sum = ::sum(ref<float>(blockSums,blockCount));
    assert_(isNumber(sum), kernel.name);
    return sum;
}

CL(sum, SSE)  float SSE(const CLVolume& A, const CLVolume& B) { return reduce2(CL::SSE, A, B); }
CL(sum, dotProduct)  float dotProduct(const CLVolume& A, const CLVolume& B) { return reduce2(CL::dotProduct, A, B); }


