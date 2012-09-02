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

template<class T> T sq(const T& x) { return x*x; }
template<class T> T cb(const T& x) { return x*x*x; }

/// Floating point primitives
inline int floor(float f) { return __builtin_floorf(f); }
inline int round(float f) { return __builtin_roundf(f); }
inline int ceil(float f) { return __builtin_ceilf(f); }

const float PI = 3.14159265358979323846;
inline float sin(float t) { return __builtin_sinf(t); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline float atan(float f) { return __builtin_atanf(f); }

/// SIMD
typedef float float4 __attribute__ ((vector_size(16)));
typedef double double2 __attribute__ ((vector_size(16)));
float4 nodebug alignedLoad(const float *p) { return *(float4*)p; }
float4 nodebug unalignedLoad(const float *p) { struct float4u { float4 v; } __attribute((__packed__, __may_alias__)); return ((float4u*)p)->v; }
#define shuffle __builtin_shufflevector
#define moveHighToLow(a,b) shuffle(a, b, 6, 7, 2, 3);

template<class T> T* allocate_aligned(int size) {
    extern "C" int posix_memalign(byte** buffer, long alignment, long size);
    byte* buffer; posix_memalign(&buffer,16,size*sizeof(T)); return (T*)buffer;
}

//TODO: store FIR of order 48 in registers
inline float inner_product_single(const float* kernel, const float* signal, int len) {
    assert_(kernel); assert_(signal); assert_(ptr(kernel)%16==0);
    float4 sum = {0,0,0,0};
    for(int i=0;i<len;i+=4) sum += alignedLoad(kernel+i) * unalignedLoad(signal+i); //TODO: align signal
    sum += moveHighToLow(sum, sum);
    sum += shuffle(sum, sum, 1,1,5,5); //== shuffle_ps 0x55 ?
    return sum[0];
}

const int filterSize = 256;
const float bandwidth = 0.975;
const int windowOversample = 32;
static double kaiser12[68] = {
    0.99859849, 1.00000000, 0.99859849, 0.99440475, 0.98745105, 0.97779076, 0.96549770, 0.95066529,
    0.93340547, 0.91384741, 0.89213598, 0.86843014, 0.84290116, 0.81573067, 0.78710866, 0.75723148,
    0.72629970, 0.69451601, 0.66208321, 0.62920216, 0.59606986, 0.56287762, 0.52980938, 0.49704014,
    0.46473455, 0.43304576, 0.40211431, 0.37206735, 0.34301800, 0.31506490, 0.28829195, 0.26276832,
    0.23854851, 0.21567274, 0.19416736, 0.17404546, 0.15530766, 0.13794294, 0.12192957, 0.10723616,
    0.09382272, 0.08164178, 0.07063950, 0.06075685, 0.05193064, 0.04409466, 0.03718069, 0.03111947,
    0.02584161, 0.02127838, 0.01736250, 0.01402878, 0.01121463, 0.00886058, 0.00691064, 0.00531256,
    0.00401805, 0.00298291, 0.00216702, 0.00153438, 0.00105297, 0.00069463, 0.00043489, 0.00025272,
    0.00013031, 0.0000527734, 0.00001000, 0.00000000 };

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

Resampler::Resampler(int channelCount, int sourceRate, int targetRate) : channelCount(channelCount) {
    int factor = gcd(sourceRate,targetRate);
    this->sourceRate = sourceRate/=factor;
    this->targetRate = targetRate/=factor;


    float cutoff;
    if (sourceRate > targetRate) { //downsampling
        cutoff = bandwidth * targetRate / sourceRate;
        N = filterSize * sourceRate / targetRate;
        N &= (~0x3); // Round down to make sure we have a multiple of 4
    } else { //upsampling
        cutoff = bandwidth;
        N = filterSize;
    }

    kernel = allocate_aligned<float>(N*targetRate);
    for(int i=0;i<targetRate;i++) {
        for (int j=0;j<N;j++) {
            kernel[i*N+j] = sinc(cutoff, (j-N/2+1)-float(i)/targetRate, N);
        }
    }

    integerAdvance = sourceRate/targetRate;
    decimalAdvance = sourceRate%targetRate;

    const int bufferSize=160;
    memSize = N-1+bufferSize;
    mem = allocate<float>(channelCount*memSize);
    clear(mem,channelCount*memSize,0.f);
}
Resampler::operator bool() const { return kernel; }
Resampler::~Resampler() { unallocate(mem,channelCount*memSize); unallocate(kernel,N*targetRate); }

void Resampler::filter(const float* source, int *sourceSize, float* target, int *targetSize, bool mix) {
    assert_(kernel); assert_(mem); assert_(source); assert_(target);
    int ilen=0, olen=0;
    for (int channel=0;channel<channelCount;channel++) {
        const float* in = source+channel;
        float* out = target+channel;
        ilen = *sourceSize;
        olen = *targetSize;
        while (ilen && olen) {
            const int filterOffset = N - 1;
            const int xlen = memSize - filterOffset;
            int ichunk = (ilen > xlen) ? xlen : ilen;
            int ochunk = olen;

            float *x = mem + channel * memSize; //TODO: avoid copy (interleaved sinc lookup)
            for(int j=0;j<ichunk;++j) x[j+filterOffset]=in[j*channelCount];

            int targetIndex = 0;
            int& integerIndex = channels[channel].integerIndex;
            int& decimalIndex = channels[channel].decimalIndex;

            while (!(integerIndex >= (int)ichunk || targetIndex >= (int)ochunk)) {
                const float *sinc = & kernel[decimalIndex*N];
                const float *iptr = & x[integerIndex];

                if(mix) out[channelCount * targetIndex++] += inner_product_single(sinc, iptr, N);
                else out[channelCount * targetIndex++] = inner_product_single(sinc, iptr, N);
                integerIndex += integerAdvance;
                decimalIndex += decimalAdvance;
                if (decimalIndex >= targetRate) {
                    decimalIndex -= targetRate;
                    integerIndex++;
                }
            }

            if (integerIndex < (int)ichunk) ichunk = integerIndex;
            ochunk = targetIndex;
            integerIndex -= ichunk;

            for(int j=0;j<N-1;++j) x[j] = x[j+ichunk];

            ilen -= ichunk;
            olen -= ochunk;
            out += ochunk * channelCount;
            in += ichunk * channelCount;
        }
    }
    *sourceSize -= ilen;
    *targetSize -= olen;
}
