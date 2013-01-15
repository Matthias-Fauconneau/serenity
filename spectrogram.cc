#include "spectrogram.h"
#include <fftw3.h>

Spectrogram::Spectrogram() {
    fftwf_init_threads();
    fftwf_plan_with_nthreads(4);
    buffer = allocate64<float>(N); clear(buffer,N);
    hann = allocate64<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
    windowed = allocate64<float>(N);
    spectrum = allocate64<float>(N);
}

void viewFrame(float* data, uint size) {
    for(uint i: range(N-size)) buffer[i] = buffer[i+size]; // Shifts buffer
    for(uint i: range(size)) buffer[N-size+i] = data[i*2+0]+data[i*2+1]; // Appends latest frame
    for(uint i: range(N)) windowed[i] = hann[i]*buffer[i]; // Multiplies window
    // Transforms
    fftwf_plan p = fftwf_plan_r2r_1d(N, windowed, spectrum, FFTW_R2HC, FFTW_ESTIMATE);
    fftwf_execute(p);
    fftwf_destroy_plan(p);
    for(int i: range(N)) spectrum[i] /= N; // Normalizes

    float Nmax = N/2; //only real part
    float Nmin = 27.0*(N/2)/synthesizer.rate;

    // Updates spectrogram
    for(uint y: range(Y)) {
        uint n0 = floor(Nmin+exp2(log2(Nmax-Nmin)*(float(y)/Y))), n1=ceil(Nmin+exp2(log2(Nmax-Nmin)*(float(y)/Y)));
        //FIXME: all bins in a pixel will have same contribution
        float sum=0;
        for(uint n=n0; n<n1; n++) {
            float a = sqrt(sqr(spectrum[n]) + sqr(spectrum[N-1-n])); // squared amplitude
            if(n>N/4) a *= 0x1p8f;
            sum += a;
        }
        sum /= n1-n0;
        int v = sRGB[clip(0,int(255*((log2(sum)-16)/8)),255)]; // Logarithmic scale from 16-24bit
        spectrogram(t,Y-1-y) = byte4(v,v,v,1);
    }

    t++; if(t>=T) t=0;
    window.render();
}
