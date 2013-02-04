#include "spectrogram.h"
#include "display.h"
#include <fftw3.h>

const float PI = 3.14159265358979323846;
inline float cos(float t) { return __builtin_cosf(t); }
inline float log2(float x) { return __builtin_log2f(x); }

Spectrogram::Spectrogram(uint N, uint rate, uint bitDepth) : ImageView(Image(F,T)), N(N), rate(rate), bitDepth(bitDepth) {
    for(uint i: range(image.stride*image.height)) (byte4&)image.data[i]=byte4(0xFF);
    buffer = allocate64<float>(N); clear(buffer,N);
    hann = allocate64<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
    windowed = allocate64<float>(N);
    spectrum = allocate64<float>(N);
    fftwf_init_threads();
    fftwf_plan_with_nthreads(4);
    plan = fftwf_plan_r2r_1d(N, windowed, spectrum, FFTW_R2HC, FFTW_ESTIMATE);
}
Spectrogram::~Spectrogram() {
    unallocate(buffer);
    unallocate(hann);
    unallocate(windowed);
    unallocate(spectrum);
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
    for(uint p: range(88)) {
        for(uint f: range(p*12,p*12+12)) {
            uint n0 = floor(pitch(21+(f-6)/12.0)/rate*N);
            uint n1 = ceil(pitch(21+(f-6+1)/12.0)/rate*N);
            if(n1==n0) continue;
            assert(n1>n0);
            float sum=0;
            for(uint n=n0; n<n1; n++) { //FIXME: weight n0 and n1 with overlap
                float a = sqrt(sqr(spectrum[n]) + sqr(spectrum[N-1-n])); // squared amplitude
                sum += a;
            }
            sum /= n1-n0;
            int v = sRGB[clip(0,int(255*((log2(sum)-(bitDepth-8))/8)),255)]; // Logarithmic scale on [-8bit 0]
            image(f,0) = byte4(0xFF-v);
        }
        uint n = round(pitch(21+p)/rate*N); //TODO: bilinear interpolation
        float intensity = sqrt(sqr(spectrum[n]) + sqr(spectrum[N-1-n]));
        pitchIntensity[t%T][p] = intensity;
    }

    // Find peaks (with temporal smoothing)
    const uint dT = 32;
    array<Peak> peaks;
    for(uint p: range(88)) {
        float intensity = 0;
        for(int dt: range(0,dT)) { //temporal smoothing
            intensity += pitchIntensity[(t+T-dt)%T][p];
        }
        intensity /= dT;

        if(intensity>0x100) {
            peaks.insertSorted( Peak __(p, intensity) );
            if(peaks.size()>6) peaks.take(0);
        }
    }
    t++;

    for(const Peak& peak : peaks) {
        for(uint f: range(peak.pitch*12,peak.pitch*12+12)) {
            for(uint t: range(dT)) image(f,t).r = 0;
        }
    }
}

void Spectrogram::render(int2 position, int2 size) {
    Locker lock(imageLock);
    int2 pos = position+(size-image.size())/2;
    blit(pos, image);

    fill(0,3*T/4,F,3*T/4+1,red); // mark current audio output
    for(uint x: range(1,88)) {
        if(x%12==3||x%12==8) fill(x*12-3,0,x*12-2,T, vec4(0,0,0,0.1)); // B/C, E/F
        else if(x%12==1||x%12==4||x%12==6||x%12==9||x%12==11) fill(x*12-3,0,(x+1)*12-3,T,vec4(0,0,0,0.1)); // black keys
    }
}
