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
    transform = allocate64<float>(N);
    spectrum = allocate64<float>(N/2);
    fftwf_init_threads();
    fftwf_plan_with_nthreads(4);
    plan = fftwf_plan_r2r_1d(N, windowed, transform, FFTW_R2HC, FFTW_ESTIMATE);
}
Spectrogram::~Spectrogram() {
    unallocate(buffer);
    unallocate(hann);
    unallocate(windowed);
    unallocate(transform);
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
    float totalEnergy=0;
    for(int i: range(N/2)) { // Converts to "power" spectrum
        float energy = sqrt(sqr(transform[i]) + sqr(transform[N-1-i]));
        spectrum[i] = energy;
        totalEnergy += energy;
    }
    totalEnergy /= N/2;
    for(int i: range(N/2)) { // Normalizes spectrum
        spectrum[i] /= totalEnergy;
    }

    // Model parameters
    const uint P = 10; // Number of maximum peaks to select
    const uint Z = 5; // Number of harmonic hypothesis for each peak
    const uint Mmax = 12; // Number of partials to search
    const float dF = exp2(1./24); // Search harmonics within a quartertone distance from ideal

    //FIXME: smooth spectrum before picking peaks

    // Spectral peak-picking (local maximum)
    struct Peak { //FIXME: use SoA layout
        uint frequency;
        float energy;
        bool operator<(const Peak& o) const {return energy<o.energy;}
    };
    array<Peak> peaks;
    for(uint i: range(1,N/2-1)) {
        if(spectrum[i-1] < spectrum[i] && spectrum[i] > spectrum[i+1]) {
            peaks.insertSorted( Peak __(i, spectrum[i]) );
        }
    }

    // Grouping peaks in combs
    struct Comb : array<Peak> {
        float energy() const { float energy = 0; for(const Peak& peak: *this) energy += peak.energy; return energy; }
    };
    array<Comb> combs;
    for(const Peak& peak: peaks.slice(max(0,int(peaks.size()-P)))) {
        const float fi = peak.frequency; //TODO: refine using phase-vocoder
        for(uint z=0; z<Z; z++) { // Harmonic rank hypothesis
            const float f0 = fi/z;
            Comb comb;
            for(uint m=0; m<Mmax; m++) { // Look for expected harmonics
                const float fm = m*f0;
                uint frequency = -1; float energy=0;
                for(const Peak& peak: peaks) {
                    if(fm/dF <= peak.frequency && peak.frequency <= fm*dF) { // Search strictly within one quatertone
                        if(peak.energy> energy) frequency = peak.frequency, energy = peak.energy;
                    }
                }
                if(frequency == uint(-1)) continue;
                comb << Peak __(frequency, energy);
            }
            combs << move(comb);
        }
    }

    // Selecting comb patterns

    // 1) Minimum support
    for(uint i=0; i<combs.size(); i++) { const Comb& comb=combs[i];
        if(comb.size()<Mmax/2) combs.removeAt(i); //at least 6 partials
        else i++;
    }

    // 2) Minimum energy
    for(uint i=0; i<combs.size(); i++) { const Comb& comb=combs[i];
        if(comb.energy()<10) combs.removeAt(i); //at least 10dB (relative to total energy)
        else i++;
    }

    // 3) Competitive energy
    float Hmax = 0;
    for(const Comb& comb: combs) Hmax = max(Hmax, comb.energy());
    for(uint i=0; i<combs.size(); i++) { const Comb& comb=combs[i];
        if(comb.energy()<0.1*Hmax) combs.removeAt(i); //at least 10% (relative to maximum hypothesis)
        else i++;
    }

    // Updates spectrogram
    for(uint p: range(88)) {
        for(uint f: range(p*12,p*12+12)) {
            uint n0 = floor(pitch(21+(f-6)/12.0)/rate*N);
            uint n1 = ceil(pitch(21+(f-6+1)/12.0)/rate*N);
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
            //for(const Peak& peak: peaks) if(peak.frequency >= n0 && peak.frequency < n1) image(f,0).r = 0;
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
