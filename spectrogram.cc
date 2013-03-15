#include "spectrogram.h"
#include "display.h"
#include <fftw3.h>

// Gamma correction
struct SRGB {
    uint8 lookup[256];
    inline float evaluate(float c) { if(c>=0.0031308) return 1.055*pow(c,1/2.4f)-0.055; else return 12.92*c; }
    SRGB() { for(uint i=0;i<256;i++) { uint l = round(255*evaluate(i/255.f)); assert(l<256); lookup[i]=l; } }
    inline uint8 operator [](uint c) { assert(c<256,c); return lookup[c]; }
};
SRGB sRGB;

const float PI = 3.14159265358979323846;
inline float cos(float t) { return __builtin_cosf(t); }

Spectrogram::Spectrogram(const Audio<float>& audio, uint periodSize, uint transformSize) :
    signal(audio.data), duration(signal.size/2), periodSize(periodSize), periodCount(duration/periodSize), N(transformSize), sampleRate(audio.rate),
    image(F, periodCount), cache(periodCount) {
    hann = buffer<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
    windowed = buffer<float>(N);
    transform = buffer<float>(N);
    spectrum = buffer<float>(N/2);
    fftwf_init_threads();
    fftwf_plan_with_nthreads(4);
    plan = fftwf_plan_r2r_1d(N, windowed, transform, FFTW_R2HC, FFTW_ESTIMATE);
}
Spectrogram::~Spectrogram() {
    fftwf_destroy_plan(plan);
}

void Spectrogram::render(int2 position, int2 size) {
    for(uint t: range(this->position, this->position+size.y)) {
        if(cache[t]) continue;

        for(uint i: range(N)) windowed[i] = hann[i]*signal[t*periodSize+i]; // Multiplies window
        fftwf_execute(plan); // Transforms
        float totalEnergy=0;
        for(int i: range(N/2)) { // Converts to "power" spectrum
            float energy = sqr(transform[i]) + sqr(transform[N-1-i]);
            spectrum[i] = energy;
            totalEnergy += energy;
        }
        totalEnergy /= N/2;
        for(int i: range(N/2)) { // Normalizes spectrum
            spectrum[i] /= totalEnergy;
        }


        // Updates spectrogram
        for(uint p: range(88)) {
            for(uint f: range(p*12,p*12+12)) {
                uint n0 = floor(pitch(21+(f-6)/12.0)/sampleRate*N);
                uint n1 = ceil(pitch(21+(f-6+1)/12.0)/sampleRate*N);
                if(n1==n0) continue;
                assert(n1>n0);
                float sum=0;
                for(uint n=n0; n<n1; n++) { //FIXME: weight n0 and n1 with overlap
                    float a = spectrum[n]; // squared amplitude
                    sum += a;
                }
                sum /= n1-n0;
                int v = sRGB[clip(0,int(sum),255)];
                image(f,0) = byte4(v);
            }
        }
    }

    blit(position+int2((size.x-image.width)/2,-this->position), image);
    //fill(0,3*T/4,F,3*T/4+1,red); // mark current audio output
    for(uint x: range(1,88)) {
        if(x%12==3||x%12==8) fill(x*12-3,0,x*12-2, size.y, vec4(0,0,0,0.1)); // B/C, E/F
        else if(x%12==1||x%12==4||x%12==6||x%12==9||x%12==11) fill(x*12-3,0,(x+1)*12-3, size.y, vec4(0,0,0,0.1)); // black keys
    }
}

/*bool Spectrogram::mouseEvent(int2 cursor, int2 size, Event event, Button button) {
    return true;
}*/
