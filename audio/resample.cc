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
#include "math.h"
#include "resample.h"
#include "string.h"

#if SIMD
#include "simd.h"
static float product(ref<float> kernel, ref<float> signal) {
    assert(kernel.size==signal.size);
    v4sf sum = {0,0,0,0};
    for(size_t i=0;i<kernel.size;i+=4) sum += loada(kernel+i) * loadu(signal+i);
    return sum[0]+sum[1]+sum[2]+sum[3];
}
#else
static float product(ref<float> kernel, ref<float> signal) {
    assert(kernel.size==signal.size);
    float sum=0; for(size_t i: range(kernel.size)) sum += kernel[i] * signal[i]; return sum;
}
#endif

static double window(float x) {
    const int windowOversample = 32;
    static constexpr double kaiser12[68] = { 0.99859849, 1.00000000, 0.99859849, 0.99440475, 0.98745105, 0.97779076, 0.96549770, 0.95066529, 0.93340547, 0.91384741, 0.89213598, 0.86843014, 0.84290116, 0.81573067, 0.78710866, 0.75723148, 0.72629970, 0.69451601, 0.66208321, 0.62920216, 0.59606986, 0.56287762, 0.52980938, 0.49704014, 0.46473455, 0.43304576, 0.40211431, 0.37206735, 0.34301800, 0.31506490, 0.28829195, 0.26276832, 0.23854851, 0.21567274, 0.19416736, 0.17404546, 0.15530766, 0.13794294, 0.12192957, 0.10723616, 0.09382272, 0.08164178, 0.07063950, 0.06075685, 0.05193064, 0.04409466, 0.03718069, 0.03111947, 0.02584161, 0.02127838, 0.01736250, 0.01402878, 0.01121463, 0.00886058, 0.00691064, 0.00531256, 0.00401805, 0.00298291, 0.00216702, 0.00153438, 0.00105297, 0.00069463, 0.00043489, 0.00025272, 0.00013031, 0.0000527734, 0.00001000, 0.00000000 };

    float y = x*windowOversample;
    int i = y;
    float t = (y-i);
    double interp[4];
    interp[3] =  -t/6 + (t*t*t)/6;
    interp[2] = t + (t*t)/2 - (t*t*t)/2;
    interp[0] = -t/3 + (t*t)/2 - (t*t*t)/6;
    interp[1] = 1-interp[3]-interp[2]-interp[0];
    return interp[0]*kaiser12[i] + interp[1]*kaiser12[i+1] + interp[2]*kaiser12[i+2] + interp[3]*kaiser12[i+3];
}
static double sinc(double cutoff, double x, int N) {
    if (abs(x)<1e-6) return cutoff;
    else if (abs(x) > N/2.0) return 0;
    double xx = x * cutoff;
    return cutoff*sin(PI*xx)/(PI*xx) * window(abs(2*x/N));
}

/// Returns the largest positive integer that divides the numbers without a remainder
int gcd(int a, int b) { while(b != 0) { int t = b; b = a % b; a = t; } return a; }

Resampler::Resampler(uint channels, uint sourceRate, uint targetRate, uint bufferSize) {
	assert_(channels==this->channels);
    // Factorize rates if possible to reduce filter bank size
    int factor = gcd(sourceRate,targetRate);
    sourceRate /= factor; targetRate /= factor;
	assert_(sourceRate); assert_(targetRate);

    // Computes filter size and cutoff
    const int filterSize = 256;
    const float bandwidth = 0.975;
    double cutoff;
    if (sourceRate > targetRate) { //downsampling
        cutoff = bandwidth * targetRate / sourceRate;
        N = filterSize * sourceRate / targetRate;
        N &= ~3; // Round down to make sure we have a multiple of 4
    } else { //upsampling
        cutoff = bandwidth;
        N = filterSize;
    }

    // Allocates and clears aligned planar signal buffers
    this->bufferSize = bufferSize = max(bufferSize,sourceRate)+N-1;
	for(uint i: range(channels)) { signal[i] = buffer<float>(bufferSize,bufferSize); signal[i].clear(0); }

    // Generates an N tap filter for each fractionnal position
    kernel = buffer<float>(targetRate*N);
    for(uint i: range(targetRate)) for(uint j: range(N)) kernel[i*N+j] = sinc(cutoff, -float(i)/targetRate+j-N/2-1, N);

    integerAdvance = sourceRate/targetRate;
    fractionalAdvance = sourceRate%targetRate;
    this->sourceRate = sourceRate, this->targetRate= targetRate;
}

template<bool mix> void Resampler::filter(const ref<float2> &source, const mref<float2> &target) {
    assert(int(source.size)>=need(target.size));
    write(source); read<mix>(target);
}
template void Resampler::filter<false>(const ref<float2> &source, const mref<float2> &target);
template void Resampler::filter<true>(const ref<float2> &source, const mref<float2> &target);

int Resampler::need(uint targetSize) {
    return (integerIndex+targetSize*integerAdvance+int(fractionalIndex+targetSize*fractionalAdvance+targetRate-1)/targetRate)-writeIndex;
}

void Resampler::write(const ref<float2>& source) {
    if(N-1+writeIndex+source.size>bufferSize) { // Wraps buffer (FIXME: map ring buffer)
        writeIndex -= integerIndex;
        for(uint channel=0;channel<channels;channel++) {
            for(uint i: range(N-1+writeIndex)) signal[channel][i] = signal[channel][integerIndex+i];
        }
        integerIndex = 0;
    }
    assert(N-1+writeIndex+source.size<=bufferSize);
    for(uint i: range(source.size)) { // Deinterleaves source to buffers
        signal[0][N-1+writeIndex+i]=source[i][0];
        signal[1][N-1+writeIndex+i]=source[i][1];
    }
     writeIndex += source.size;
     assert(writeIndex<bufferSize);
}

size_t Resampler::available() {
    size_t available = ((writeIndex-integerIndex)*targetRate-fractionalIndex)/sourceRate;
    assert(int(available)>=0 && (need(available)==0 || (sourceRate>targetRate && need(available)==-1)));
    return available;
}

template<bool mix> void Resampler::read(const mref<float2>& target) {
    assert(target.size<=available());
    for(uint i: range(target.size)) {
        for(uint channel=0;channel<channels;channel++) {
            if(mix) target[i][channel] += product(kernel.slice(fractionalIndex*N, N), signal[channel].slice(integerIndex, N));
            else    target[i][channel]    = product(kernel.slice(fractionalIndex*N, N), signal[channel].slice(integerIndex, N));
        }
        integerIndex += integerAdvance;
        fractionalIndex += fractionalAdvance;
        if(fractionalIndex >= targetRate) {
            fractionalIndex -= targetRate;
            integerIndex++;
        }
        assert(integerIndex<writeIndex || (integerIndex==writeIndex && fractionalIndex==0), integerIndex, writeIndex, fractionalIndex);
        assert(fractionalIndex<targetRate);
    }
    //assert(integerIndex>=writeIndex-2);
}
template void Resampler::read<false>(const mref<float2>& target);
template void Resampler::read<true>(const mref<float2>& target);

void Resampler::clear() { writeIndex=0, integerIndex=0, fractionalIndex=0; }
