#include "thread.h"
#include "math.h"
#include "pitch.h"
#include <fftw3.h> //fftw3f
#include "flac.h"
#include "plot.h"
#include "layout.h"
#include "window.h"

/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation {
    VList<Plot> plots;
    Window window {&plots, int2(1024, 768), "Tuner"};
    const uint rate = 48000;
    static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
    PitchEstimator pitchEstimator {N};
    float previousPowers[2] = {0,0};

    PitchEstimation() {
        map<float, float> spectrumPlot;
        map<float,float> offsets;

        buffer<float2> stereo = decodeAudio(Map("Samples/3.flac"_));
        assert_(stereo.size/rate/5==28);

        Notch notch0(50./48000, 1); // Notch filter to remove 50Hz noise
        Notch notch1(150./48000, 1); // Cascaded to remove the first odd harmonic (3rd partial)

        int lastKey = 42+12+1;
        for(uint t=N; t<=stereo.size-N; t+=N) {
            const float2* period = stereo + t;
            float signal[N]; for(uint i: range(N)) signal[i] = notch0(notch1( (period[i][0]+period[i][1]) * 0x1p-25f ));

            int expectedKey = 75 - t/rate/5; // Benchmark is one key every 5 seconds descending from D#5 to C2

            const uint fMin = N*440*exp2(-4 - 0./12 - (1./2 / 12))/rate; // ~27 Hz ~ half pitch under A-1
            const uint fMax = N*440*exp2(3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half pitch over C7
            float k = pitchEstimator.estimate(signal, fMin, fMax);
            float expectedK = rate/keyToPitch(expectedKey);
            const float kOffset = 12*log2(expectedK/k);
            const float kError = 12*log2((expectedK+1)/expectedK);

            float expectedF = keyToPitch(expectedKey)*N/rate;
            float f = pitchEstimator.fPeak / pitchEstimator.period;
            const float fOffset =  12*log2(f/expectedF);
            const float fError =  12*log2((expectedF+1)/expectedF);

            float power = pitchEstimator.power;
            int key = round(pitchToKey(rate/k));

            if(log2(power) > -8 && previousPowers[1] > power && previousPowers[0] > previousPowers[1]/2) {
                log(strKey(expectedKey)+"\t"_+strKey(max(0,key))+"\t"_ +str(round(rate/k))+" Hz\t"_
                    +dec(round(100*kOffset))+" \t+/-"_+dec(round(100*kError))+" cents\t"_
                    +dec(round(100*fOffset))+" \t+/-"_+dec(round(100*fError))+" cents\t"_
                    +dec(log2(previousPowers[0]))+"\t"_+dec(log2(previousPowers[1]))+"\t"_+dec(log2(power))+"\t"_
                    +(expectedKey==key && (key==lastKey || key+1==lastKey) ? ""_ :"X"_));
                offsets.insertMulti(key-42, 100*(fError<kError ? fOffset : kOffset));
                if(expectedKey!=key && !spectrumPlot) for(uint i: range(16, 4096*N/rate)) {
                    float e = pitchEstimator.spectrum[i];
                    if(e>1./16) spectrumPlot.insert(i*rate/N, e);
                }
                lastKey = key;
            }

            previousPowers[0] = previousPowers[1];
            previousPowers[1] = power;
        }

        if(offsets) {Plot plot;
            plot.xlabel = String("Key"_), plot.ylabel = String("Cents"_);
            plot.dataSets << move(offsets);
            plots << move(plot);
        }
        if(spectrumPlot) {Plot plot;
            plot.title = String("Log spectrum"_);
            plot.xlabel = String("Frequency"_), plot.ylabel = String("Energy"_);
            plot.dataSets << copy(spectrumPlot);
            plot.log[0] = true; plot.log[1] = true;
            plots << move(plot);
        }
        if(plots) {
            window.backgroundColor=window.backgroundCenter=1;
            window.show();
            window.localShortcut(Escape).connect([]{exit();});
        }
    }
} app;
