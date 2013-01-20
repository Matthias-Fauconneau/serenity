#include "spectrogram.h"
#include "display.h"
#include <fftw3.h>

const float PI = 3.14159265358979323846;
inline float cos(float t) { return __builtin_cosf(t); }
inline float log2(float x) { return __builtin_log2f(x); }

Spectrogram::Spectrogram(uint N, uint rate, uint bitDepth) : ImageView(Image(F,T)), N(N), rate(rate), bitDepth(bitDepth) {
    buffer = allocate64<float>(N); clear(buffer,N);
    hann = allocate64<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
    windowed = allocate64<float>(N);
    spectrum = allocate64<float>(N);
    fftwf_init_threads();
    fftwf_plan_with_nthreads(4);
    plan = fftwf_plan_r2r_1d(N, windowed, spectrum, FFTW_R2HC, FFTW_ESTIMATE);
}
Spectrogram::~Spectrogram() {
    unallocate(buffer,N);
    unallocate(hann,N);
    unallocate(windowed,N);
    unallocate(spectrum,N);
    fftwf_destroy_plan(plan);
}

void Spectrogram::write(int16* data, uint size) {
    for(uint i: range(N-size)) buffer[i] = buffer[i+size]; // Shifts buffer
    for(uint i: range(size)) buffer[N-size+i] = data[i*2+0]+data[i*2+1]; // Appends latest frame
    update();
}

void Spectrogram::write(int32* data, uint size) {
    for(uint i: range(N-size)) buffer[i] = buffer[i+size]; // Shifts buffer
    for(uint i: range(size)) buffer[N-size+i] = data[i*2+0]+data[i*2+1]; // Appends latest frame
    update();
}

void Spectrogram::write(float* data, uint size) {
    for(uint i: range(N-size)) buffer[i] = buffer[i+size]; // Shifts buffer
    for(uint i: range(size)) buffer[N-size+i] = data[i*2+0]+data[i*2+1]; // Appends latest frame
    update();
}

void Spectrogram::update() {
    // Shifts image down
    Locker lock(imageLock);
    for(uint y=image.height-1; y>=1; y--)
        for(uint x: range(image.width))
            image(x,y) = image(x,y-1);

    for(uint i: range(N)) windowed[i] = hann[i]*buffer[i]; // Multiplies window
    fftwf_execute(plan); // Transforms
    for(int i: range(N)) spectrum[i] /= N; // Normalizes

    // Updates spectrogram
    for(uint f: range(F)) {
        uint n0 = floor(pitch(21+(f-6)/12.0)/rate*N);
        uint n1 = ceil(pitch(21+(f-6+1)/12.0)/rate*N);
        float sum=0;
        for(uint n=n0; n<n1; n++) { //FIXME: weight n0 and n1 with overlap
            float a = sqrt(sqr(spectrum[n]) + sqr(spectrum[N-1-n])); // squared amplitude
            sum += a;
        }
        sum /= n1-n0;
        int v = sRGB[clip(0,int(255*((log2(sum)-(bitDepth-8))/8)),255)]; // Logarithmic scale on [-8bit 0]
        image(f,/*t*/0) = byte4(v,v,v,255);
    }
    //t++;
}

void Spectrogram::render(int2 position, int2 size) {
    Locker lock(imageLock);
    /*if(t > 0) {
        // Shifts image down
        for(uint y=image.height-1; y>=t; y--)
            for(uint x: range(image.width))
                image(x,y) = image(x,y-t);
        t = 0;
    }*/

    int2 pos = position+(size-image.size())/2;
    blit(pos, image);

    fill(0,T/2,F,T/2+1,red); // mark current audio output
    for(uint x: range(1,88)) fill(x*12,0,x*12+1,T,blue); // mark current audio output
}
