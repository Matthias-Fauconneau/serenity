/* Copyright (C) 2007-2008 Jean-Marc Valin, Thorvald Natvig

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

   1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
   IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
   OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
   (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
   HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
   STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
   POSSIBILITY OF SUCH DAMAGE.
*/
#include "resample.h"
#include "memory.h"
#include "debug.h"

template<class T> T sq(const T& x) { return x*x; }
template<class T> T cb(const T& x) { return x*x*x; }

/// Trigonometric primitives
const float PI = 3.14159265358979323846;
inline float sin(float t) { return __builtin_sinf(t); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline float atan(float f) { return __builtin_atanf(f); }

/// SIMD
typedef float float4 __attribute__ ((vector_size(16)));
inline float4 nodebug alignedLoad(const float *p) { return *(float4*)p; }
#if __clang__
#define shuffle __builtin_shufflevector
#define moveHighToLow(a,b) shuffle(a, b, 6, 7, 2, 3);
inline float4 nodebug unalignedLoad(const float *p) { struct float4u { float4 v; } __attribute((__packed__, __may_alias__)); return ((float4u*)p)->v; }
#else
#define unalignedLoad __builtin_ia32_loadups
#define extract_s __builtin_ia32_vec_ext_v4sf
#define moveHighToLow(a,b) __builtin_ia32_movhlps(a,b)
#define shuffle_ps __builtin_ia32_shufps
#endif
extern "C" int posix_memalign(byte** buffer, long alignment, long size);
template<class T> T* allocate_aligned(int size) { byte* buffer; check_(posix_memalign(&buffer,16,size*sizeof(T))); return (T*)buffer; }

//TODO: store filter in 15x4 registers
inline float product(const float* kernel, const float* signal, int len) {
    assert_(kernel); assert_(signal); assert_(ptr(kernel)%16==0);
    float4 sum = {0,0,0,0};
    for(int i=0;i<len;i+=4) sum += alignedLoad(kernel+i) * unalignedLoad(signal+i); //TODO: align signal
    sum += moveHighToLow(sum, sum);
#if __clang__
    sum += shuffle(sum, sum, 1,1,5,5);
    return sum[0];
#else
    sum += shuffle_ps(sum, sum, 0x55);
    return extract_s(sum, 0);
#endif
}

// Speex Q10 (highest quality)
const int filterSize = 256;
const float bandwidth = 0.975;
const int windowOversample = 32;
static double kaiser12[68] = { 0.99859849, 1.00000000, 0.99859849, 0.99440475, 0.98745105, 0.97779076, 0.96549770, 0.95066529, 0.93340547, 0.91384741, 0.89213598, 0.86843014, 0.84290116, 0.81573067, 0.78710866, 0.75723148, 0.72629970, 0.69451601, 0.66208321, 0.62920216, 0.59606986, 0.56287762, 0.52980938, 0.49704014, 0.46473455, 0.43304576, 0.40211431, 0.37206735, 0.34301800, 0.31506490, 0.28829195, 0.26276832, 0.23854851, 0.21567274, 0.19416736, 0.17404546, 0.15530766, 0.13794294, 0.12192957, 0.10723616, 0.09382272, 0.08164178, 0.07063950, 0.06075685, 0.05193064, 0.04409466, 0.03718069, 0.03111947, 0.02584161, 0.02127838, 0.01736250, 0.01402878, 0.01121463, 0.00886058, 0.00691064, 0.00531256, 0.00401805, 0.00298291, 0.00216702, 0.00153438, 0.00105297, 0.00069463, 0.00043489, 0.00025272, 0.00013031, 0.0000527734, 0.00001000, 0.00000000 };

static double window(float x) {
    float y = x*windowOversample;
    int i = floor(y);
    float t = (y-i);
    double interp[4];
    interp[3] =  -t/6 + (t*t*t)/6;
    interp[2] = t + (t*t)/2 - (t*t*t)/2;
    interp[0] = -t/3 + (t*t)/2 - (t*t*t)/6;
    interp[1] = 1-interp[3]-interp[2]-interp[0];
    return interp[0]*kaiser12[i] + interp[1]*kaiser12[i+1] + interp[2]*kaiser12[i+2] + interp[3]*kaiser12[i+3];
}
static float sinc(double cutoff, double x, int N) {
    if (abs(x)<1e-6) return cutoff;
    else if (abs(x) > N/2.0) return 0;
    double xx = x * cutoff;
    return cutoff*sin(PI*xx)/(PI*xx) * window(abs(2*x/N));
}

/// Returns the largest positive integer that divides the numbers without a remainder
inline int gcd(int a, int b) { while(b != 0) { int t = b; b = a % b; a = t; } return a; }

Resampler::Resampler(uint channelCount, uint sourceRate, uint targetRate) {
    assert_(channelCount==this->channelCount);

    // Computes filter size and cutoff
    float cutoff;
    if (sourceRate > targetRate) { //downsampling
        cutoff = bandwidth * targetRate / sourceRate;
        N = filterSize * sourceRate / targetRate;
        N &= (~0x3); // Round down to make sure we have a multiple of 4
    } else { //upsampling
        cutoff = bandwidth;
        N = filterSize;
    }

    // Allocates and clears aligned planar signal buffers
    bufferSize = sourceRate+N-1;
    for(uint i=0;i<channelCount;i++) {
        buffer[i] = allocate_aligned<float>(bufferSize);
        clear(buffer[i],bufferSize,0.f);
    }

    // Factorize rates if possible to reduce filter bank size
    int factor = gcd(sourceRate,targetRate);
    this->sourceRate=sourceRate/=factor; this->targetRate=targetRate/=factor;
    integerAdvance = sourceRate/targetRate;
    fractionalAdvance = sourceRate%targetRate;

    // Generates an N tap filter for each fractionnal position
    kernel = allocate_aligned<float>(targetRate*N);
    for(uint i=0;i<targetRate;i++) for(uint j=0;j<N;j++) kernel[i*N+j] = sinc(cutoff, -float(i)/targetRate+(float(j)-(N/2+1)), N);
}
Resampler::~Resampler() {
    for(int i=0;i<channelCount;i++) if(buffer[i]) unallocate(buffer[i],channelCount*bufferSize);
    if(kernel) unallocate(kernel,N*targetRate);
}

template<bool mix>
void Resampler::filter(const float* source, uint sourceSize, float* target, uint targetSize) {
    assert_(kernel); assert_(buffer); assert_(source); assert_(target); assert(sourceSize==bufferSize-(N-1),sourceSize,bufferSize-(N-1));
    assert(sourceSize%sourceRate==0,sourceSize,sourceRate);
    assert(targetSize%targetRate==0,targetSize,targetRate);
    assert(sourceSize/sourceRate*targetRate==targetSize);
    for(uint channel=0;channel<channelCount;channel++) {
        // Copies interleaved signal into aligned planar buffer
        for(uint j=0;j<sourceSize;j++) buffer[channel][j+N-1]=source[j*channelCount+channel];

        // Convolves the signal with the kernel
        uint integerIndex=0,fractionalIndex=0;
        for(uint i=0;i<targetSize;i++) {
            if(mix) target[i*channelCount+channel] += product(kernel+fractionalIndex*N, buffer[channel]+integerIndex, N);
            else     target[i*channelCount+channel]   = product(kernel+fractionalIndex*N, buffer[channel]+integerIndex, N);
            integerIndex += integerAdvance;
            fractionalIndex += fractionalAdvance;
            if(fractionalIndex >= targetRate) {
                fractionalIndex -= targetRate;
                integerIndex++;
            }
        }
        assert(integerIndex == sourceSize,integerIndex,sourceSize,targetSize); assert_(fractionalIndex == 0);

        // Copies the last samples for next period (TODO: ring buffer)
        for(uint j=0;j<N-1;++j) buffer[channel][j] = buffer[channel][j+sourceSize];
    }
}
template void Resampler::filter<false>(const float* source, uint sourceSize, float* target, uint targetSize);
template void Resampler::filter<true>(const float* source, uint sourceSize, float* target, uint targetSize);
