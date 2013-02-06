#include "spectrogram.h"
#include "display.h"
#include <fftw3.h>

const float PI = 3.14159265358979323846;
inline float cos(float t) { return __builtin_cosf(t); }

byte4 hsv(float h, float s, float v, float a=1) {
 v *= 0xFF; a *= 0xFF;
 if(s==0.0) return byte4(v, v, v, a);
 if(h==1.0) h = 0.0;
 h *= 6.0;
 float i = floor(h);
 float f = h - i;
 float p = v*(1.0-s);
 float q = v*(1.0-(s*f));
 float t = v*(1.0-(s*(1.0-f)));
 if(i == 0.0) return byte4(v, t, p, a);
 if(i == 1.0) return byte4(q, v, p, a);
 if(i == 2.0) return byte4(p, v, t, a);
 if(i == 3.0) return byte4(p, q, v, a);
 if(i == 4.0) return byte4(t, p, v, a);
 return byte4(v, p, q, a);
}

Spectrogram::Spectrogram(uint N, uint rate, uint bitDepth) : ImageView(Image(F,T)), N(N), rate(rate), bitDepth(bitDepth) {
    for(uint i: range(image.stride*image.height)) (byte4&)image.data[i]=byte4(0xFF);
    buffer = allocate64<float>(N); clear(buffer,N);
    hann = allocate64<float>(N); for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2;
    windowed = allocate64<float>(N);
    transform = allocate64<float>(N);
    rawSpectrum = allocate64<float>(N/2);
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
    unallocate(rawSpectrum);
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

struct Peak { //FIXME: use SoA layout
    uint frequency;
    float energy;
    bool operator<(const Peak& o) const {return energy<o.energy;}
};
string str(const Peak& peak) { return str(peak.frequency); }

struct Comb : array<Peak> {
    float f0;
    Comb(float f0):f0(f0){}
    float energy() const { float energy = 0; for(const Peak& peak: *this) energy += peak.energy; return energy; }
};
string str(const Comb& comb) { return str(int(comb.f0)); }

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
        float energy = sqr(transform[i]) + sqr(transform[N-1-i]);
        rawSpectrum[i] = energy;
        totalEnergy += energy;
    }
    totalEnergy /= N/2;
    for(int i: range(N/2)) { // Normalizes spectrum
        rawSpectrum[i] /= totalEnergy;
    }

    // Model parameters
#if 1
    const uint P = 10; // Number of maximum peaks to select
    const uint Z = 1; // Number of harmonic hypothesis for each peak
    const uint M = 12; // Number of partials to search
#else
    const uint P = 6;//10; // Number of maximum peaks to select
    const uint Z = 1;//5; // Number of harmonic hypothesis for each peak
    const uint M = 8;//12; // Number of partials to search
#endif
    const float dF = exp2(1./24); // Search harmonics within a quartertone distance from ideal

#if SMOOTH
    constexpr int S = 1;
    for(uint i: range(S,N/2-S)) { // Smooth spectrum with a box filter
        float sum=0; for(int di=-S; di<=S; di++) sum += rawSpectrum[i+di];
        spectrum[i] = sum/(S+1+S);
    }
#else
    #define spectrum rawSpectrum
#endif

    // Spectral peak-picking (local maximum)
    array<Peak> peaks;
    constexpr int S = 9; // Range to search local maximum (FIXME: "tent" search instead of "box" search)
    for(uint i: range(S,N/2-S)) {
        for(int di=-S; di<=S; di++) if(spectrum[i+di] > spectrum[i]) goto break_;
        /*else*/ {
            peaks.insertSorted( Peak __(i, spectrum[i]) );
            if(peaks.size()>P) peaks.take(0);
        }
        break_: ;
        /*if(spectrum[i-1] < spectrum[i] && spectrum[i] > spectrum[i+1]) {
            peaks.insertSorted( Peak __(i, spectrum[i]) );
            if(peaks.size()>P) peaks.take(0);
        }*/
    }

    // Grouping peaks in combs
    array<Comb> combs;
    for(const Peak& peak: peaks) {
        const float fi = peak.frequency; //TODO: refine using phase-vocoder
        for(uint z=1; z<=Z; z++) { // Harmonic rank hypothesis
            const float f0 = fi/z;
            Comb comb __(f0);
            for(uint m=1; m<=M; m++) { // Look for expected harmonics
                const float fm = m*f0;
                assert(ceil(fm/dF)>0);
                if(floor(fm*dF)>=N/2) continue;
                uint frequency = -1; float energy=0;
                for(uint k: range(ceil(fm/dF),floor(fm*dF)+1)) { // Search strictly within one quatertone
                    if(spectrum[k-1] < spectrum[k] && spectrum[k] > spectrum[k+1] && spectrum[k] > energy)
                        frequency = k, energy = spectrum[k];
                }
                if(frequency == uint(-1)) continue;
                comb << Peak __(frequency, energy);
            }
            combs << move(comb);
        }
    }

    // Selecting comb patterns
    uint stats[7];
    stats[0]=combs.size();

    // 1) Minimum support
    for(uint i=0; i<combs.size();) { const Comb& comb=combs[i];
        if(comb.size()<M/2) { combs.removeAt(i); continue; } //at least 6 partials
        i++;
    }
    stats[1]=combs.size();

    // 2) Minimum energy
    for(uint i=0; i<combs.size();) { const Comb& comb=combs[i];
        if(comb.energy()<10) { combs.removeAt(i); continue; } //at least 10dB (relative to total energy)
        i++;
    }
    stats[2]=combs.size();

    // 3) Detection of sub-harmonics
    for(uint i=0; i<combs.size();) { const Comb& low=combs[i];
        for(uint j=0; j<combs.size(); j++) { const Comb& high=combs[j];
            if(abs(high.f0/low.f0-2)<0.001) { // octave-related pair (high partials are all even partials of low)
                // Remove low if
                // - Even partials energy > 3 · Odd partials energy
                float even=0; for(uint m=0; m<low.size(); m+=2) even += low[m].energy;
                float odd=0; for(uint m=1; m<low.size(); m+=2) odd += low[m].energy;
                if(even > 3*odd) goto remove;
                // - Lower M/4 partials energy < Total energy / 8
                float lower=0; for(uint m=0; m<low.size()/4; m++) lower += low[m].energy;
                float total=0; for(uint m=0; m<low.size(); m++) total += low[m].energy;
                if(lower < total/8) goto remove;
            }
        }
        i++; continue;
        remove: combs.removeAt(i);
    }
    stats[3]=combs.size();

    // 4) Detection of overtones
    for(uint i=0; i<combs.size(); i++) { const Comb& low=combs[i];
        for(uint j=0; j<combs.size();) { const Comb& high=combs[j];
            if(abs(high.f0/low.f0-2)<0.001) { // octave-related pair (high partials are all even partials of low)
                // Remove high if M/4 partials energy > Total energy / 2
                float lower=0; for(uint m=0; m<high.size()/4; m++) lower += high[m].energy;
                float total=0; for(uint m=0; m<high.size(); m++) total += high[m].energy;
                if(lower > total/2) { combs.removeAt(j); continue; }
            }
            j++;
        }
    }
    stats[4]=combs.size();

    // 5) Harmonic overlapping
    for(uint i=0; i<combs.size();) { const Comb& X=combs[i];
        float original = 0; // Energy of peaks weighted down when found in other hypotheses
        for(const Peak& x : X) {
            uint shares = 0;
            for(const Comb& O: combs) for(const Peak& o : O) { if(o.frequency == x.frequency) { assert(o.energy == x.energy); shares++; } }
            original += x.energy / shares;
        }
        if(original < X.energy() / 2) { combs.removeAt(i); continue; }
        i++;
    }
    stats[5]=combs.size();

    // 6) Competitive energy
    float Hmax = 0;
    for(const Comb& comb: combs) Hmax = max(Hmax, comb.energy());
    for(uint i=0; i<combs.size();) { const Comb& comb=combs[i];
        if(comb.energy()<0.1*Hmax) { combs.removeAt(i); continue; } //at least 10% (relative to maximum hypothesis)
        i++;
    }
    stats[6]=combs.size();

    //log(stats[0],stats[0]-stats[1],stats[1]-stats[2],stats[2]-stats[3],stats[4]-stats[5],stats[5]-stats[6],stats[6]);

    // Shifts notes down
    for(uint t=T-1; t>=1; t--) notes[t] = move(notes[t-1]);
    notes[0].clear();
    for(const Comb& comb: combs) {
        uint key = round(::key(comb.f0*rate/N));
        for(const Comb& combO : combs) { if(&comb==&combO) continue;
            uint keyO = round(::key(combO.f0*rate/N));
            if(key == keyO) {
                static bool once = false; if(once) break; once = true;
                log("duplicate hypothesis", int(comb.f0), int(combO.f0), combs);
                //for(const Comb& comb: combs) log(int(comb.f0), comb.f0*rate/N, round(::key(comb.f0*rate/N)), (array<Peak>&)comb);
            }
        }
        assert(!notes[0].contains(key));
        notes[0] << key;
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
            /*for(uint i=0; i<combs.size(); i++) { const Comb& comb=combs[i];
                for(const Peak& peak: comb)
                    if(peak.frequency >= n0 && peak.frequency < n1)
                        image(f,0) = byte4((3*int4(image(f,0))+1*int4(hsv(float(i)/combs.size(),1,1)))/4);
                if(comb.f0 >= n0 && comb.f0 < n1) {
                    image(f,0) = hsv(float(i)/combs.size(),1,1);
                }
            }*/
            for(uint i=0; i<notes[0].size(); i++) {
                //if(21+p == notes[0][i]) image(f,0) = hsv(float(i)/combs.size(),1,1);
                if(21+p == notes[0][i]) image(f,0) = byte4((1*int4(image(f,0))+1*int4(hsv(float(i)/combs.size(),1,1)))/2);
            }
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
