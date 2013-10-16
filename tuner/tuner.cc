#include "thread.h"
#include "sampler.h"
#include "math.h"
#include <fftw3.h> //fftw3f
typedef struct fftwf_plan_s* fftwf_plan;
struct FFTW : handle<fftwf_plan> { using handle<fftwf_plan>::handle; default_move(FFTW); FFTW(){} ~FFTW(); };
FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }
struct FFT {
    uint N;
    buffer<float> hann {N};
    buffer<float> windowed {N};
    buffer<float> halfcomplex {N};
    FFTW fftw = fftwf_plan_r2r_1d(N, windowed.begin(), halfcomplex.begin(), FFTW_R2HC, FFTW_ESTIMATE);
    FFT(uint N) : N(N) { for(uint i: range(N)) hann[i] = (1-cos(2*PI*i/(N-1)))/2; }
    buffer<float> transform(const ref<float2>& signal) {
        assert(N <= signal.size);
        for(uint i: range(N)) windowed[i] = hann[i]*(signal[i][0]+signal[i][1])/(2<<(24+1)); // Multiplies window
        fftwf_execute(fftw); // Transforms (FIXME: use execute_r2r and free windowed/transform buffers between runs)
        buffer<float> spectrum {N/2};
        for(int i: range(N/2)) spectrum[i] = sq(halfcomplex[i]) + sq(halfcomplex[N-1-i]); // Converts to intensity spectrum
        return spectrum;
    }
};

inline float keyToPitch(float key) { return 440*exp2((key-69)/12); }
inline float pitchToKey(float pitch) { return 69+log2(pitch/440)*12; }

/// Estimates fundamental frequency (~pitch) of test samples
struct PitchEstimation {
    PitchEstimation() {
        Sampler sampler;
        const uint sampleRate = 48000;
        sampler.open(sampleRate, "Salamander.sfz"_, Folder("Samples"_,root()));

        int tests = 0;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger==0 && sample.lovel <= 64 && 64 <= sample.hivel) {}
            else continue;
            tests++;
        }

        const int periodMax = 3; //16K, 32K, 64K
        const int smoothMax = 12; // 9Hz
        const int octaveMax = 3;
        byte result[periodMax][octaveMax][smoothMax+1][tests];
        clear((byte*)result, periodMax*octaveMax*(smoothMax+1)*tests);
        uint testIndex=0;
        for(const Sample& sample: sampler.samples) {
            if(sample.trigger==0 && sample.lovel <= 64 && 64 <= sample.hivel) {}
            else continue;
            testIndex++;
            int key = sample.pitch_keycenter;
            float expectedF0 = keyToPitch(key);
            log(key, expectedF0, sample.lovel, sample.hivel);
            assert(round(pitchToKey(expectedF0))==key);

            assert(65536<=sample.flac.duration);
            buffer<float2> signal = decodeAudio(sample.data, 65536);
            for(uint period: range(periodMax)) {
                static FFT ffts[3] = {16384,32768,65536};
                FFT& fft = ffts[period];
                uint N = fft.N;
                buffer<float> spectrum = fft.transform(signal);

                // Estimates fundamental frequency using spectral harmonic correlation
                for(uint octaveCount=1; octaveCount<=octaveMax; octaveCount++) {
                    for(int smoothRadius=0; smoothRadius<=smoothMax; smoothRadius++) {
                        float maximum = 0, f0 = 0;
                        for(int i: range(smoothRadius,N/2/octaveCount-smoothRadius)) {
                            float s=0;
                            for(int di=-smoothRadius; di<=smoothRadius; di++) {
                                float p=1;
                                for(uint r=1; r<=octaveCount; r++) p *= spectrum[r*i+di];
                                s += p;
                            }
                            if(s>maximum) maximum=s, f0=(float)i*sampleRate/N;
                        }
                        //TODO: NCCF around f0 and f0/2 (+ f0*2?, +more candidates (+median))
                        result[period][octaveCount-1][smoothRadius][testIndex] = round(pitchToKey(f0))==key;
                    }
                }
            }
        }
        int bestScore=0;
        for(uint period: range(periodMax)) {
            for(uint octaveCount=1; octaveCount<=octaveMax; octaveCount++) { // 1: Estimates directly from intensity
                for(int smoothRadius=0; smoothRadius<=smoothMax; smoothRadius++) {
                    int success=0; for(int i : range(tests)) success += result[period][octaveCount-1][smoothRadius][i];
                    bestScore = max(bestScore, success);
                }
            }
        }
        for(uint period: range(periodMax)) {
            for(uint octaveCount=1; octaveCount<=octaveMax; octaveCount++) { // 1: Estimates directly from intensity
                for(int smoothRadius=0; smoothRadius<=smoothMax; smoothRadius++) {
                    int success=0; buffer<char> detail(tests);
                    for(int i : range(tests)) {
                        bool ok = result[period][octaveCount-1][smoothRadius][i];
                        success += ok;
                        detail[i] = ok ? 'O' : 'X';
                    }
                    if(success >= bestScore*14/15)
                        log("N",16384*pow(2,period), "\tO",octaveCount, "\tR",smoothRadius, "\t->", success, "\t", detail);
                }
            }
        }
        log("Best",bestScore,"/",tests,"~",str((int)round(100.*bestScore/tests))+"%"_,"for range ",64,"-",64);
    }
} test;
