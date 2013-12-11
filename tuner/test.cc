#include "thread.h"
#include "math.h"
#include "pitch.h"
#include "ffmpeg.h"
#include "data.h"
#include "time.h"
#include "layout.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "profile.h"
#include <fftw3.h> //fftw3f

int parseKey(const string& name) {
    TextData s(name);
    int key=24;
    key += "c#d#ef#g#a#b"_.indexOf(toLower(s.next()));
    if(s.match('#')) key++;
    key += 12*s.decimal();
    return key;
}

void writeWaveFile(const string& path, const ref<int32>& data, int32 rate, int channels) {
    File file(path,home(),Flags(WriteOnly|Create|Truncate));
    struct { char RIFF[4]={'R','I','F','F'}; int32 size; char WAVE[4]={'W','A','V','E'}; char fmt[4]={'f','m','t',' '};
             int32 headerSize=16; int16 compression=1; int16 channels; int32 rate; int32 bps;
                  int16 stride; int16 bitdepth=32; char data[4]={'d','a','t','a'}; } packed header;
    header.size = sizeof(header) + data.size*sizeof(int32);
    header.channels = channels;
    header.rate = rate;
    header.bps = rate*channels*sizeof(int32);
    header.stride = channels*sizeof(int32);
    file.write(raw(header));
    file.write(cast<byte>(data));
}

struct Plot : Widget {
    const bool logx, logy; // Whether to use log scale on x/y axis
    const float resolution; // Resolution in bins / Hz
    uint iMin = 0, iMax = 0; // Displayed range in bins
    ref<float> spectrum; // Displayed power spectrum
    ref<float> unfilteredSpectrum; // Displayed power spectrum
    // Additional analysis data
    const PitchEstimator& estimator;
    float expectedF = 0;

    Plot(bool logx, bool logy, float resolution, ref<float> spectrum, ref<float> unfilteredSpectrum, const PitchEstimator& estimator):
        logx(logx),logy(logy),resolution(resolution),spectrum(spectrum),unfilteredSpectrum(unfilteredSpectrum),estimator(estimator){}
    float x(float f) { return logx ? (log2(f)-log2((float)iMin))/(log2((float)iMax)-log2((float)iMin)) : (float)(f-iMin)/(iMax-iMin); }
    void render(int2 position, int2 size) {
        assert_(iMin < iMax && iMax <= spectrum.size, iMin, iMax, spectrum.size);
        if(iMin >= iMax) return;
        float sMax = -inf; for(uint i: range(iMin, iMax)) sMax = max(sMax, spectrum[i]);
        float sMin = estimator.periodPower;

        {float y = (logy ? (log2(estimator.noiseThreshold*sMin) - log2(sMin)) / (log2(sMax)-log2(sMin)) : (2*sMin / sMax)) * size.y;
            line(position.x,position.y+size.y-y-0.5,position.x+size.x,position.y+size.y-y+0.5,vec4(0,1,0,1));}
        {float y = (logy ? (log2(estimator.highPeakThreshold*sMin) - log2(sMin)) / (log2(sMax)-log2(sMin)) : (2*sMin / sMax)) * size.y;
            line(position.x,position.y+size.y-y-0.5,position.x+size.x,position.y+size.y-y+0.5,vec4(0,1,0,1));}

        for(uint i: range(iMin, iMax)) { // Unfiltered energy density
            float x0 = x(i) * size.x;
            float x1 = x(i+1) * size.x;
            float y = (logy ? (unfilteredSpectrum[i] ? (log2(unfilteredSpectrum[i]) - log2(sMin)) / (log2(sMax)-log2(sMin)) : 0) :
                              (unfilteredSpectrum[i] / sMax)) * size.y;
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,vec4(1,1,1,0.5));
        }

        for(uint i: range(iMin, iMax)) { // Energy density
            float x0 = x(i) * size.x;
            float x1 = x(i+1) * size.x;
            float y = (logy ? (spectrum[i] ? (log2(spectrum[i]) - log2(sMin)) / (log2(sMax)-log2(sMin)) : 0) : (spectrum[i] / sMax)) * size.y;
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,vec4(1,1,1,1));
        }

        for(PitchEstimator::Peak peak: estimator.peaks) { // Peaks
            float f = peak.f;
            float x = this->x(f+0.5)*size.x;
            line(position.x+x,position.y,position.x+x,position.y+size.y,vec4(1,1,1,1./4));
            Text label("·",16, vec4(1,1,1,1));
            label.render(int2(position.x+x,position.y+16));
        }

        { // F1
            float f = estimator.F1;
            float x = this->x(f+0.5)*size.x;
            //line(position.x+x,position.y,position.x+x,position.y+size.y,vec4(1,1,1,1));
            Text label(str(estimator.nHigh),16, vec4(1,1,1,1));
            label.render(int2(position.x+x,position.y+16));
        }

        for(uint i: range(estimator.candidates.size)) {
            const auto& candidate = estimator.candidates[i];
            bool best = i==estimator.candidates.size-1;
            vec4 color(!best,best,0,1);
            float Ea = estimator.candidates.last().lastEnergy;
            float Na = estimator.candidates.last().lastHarmonicRank;
            float Eb = estimator.candidates[i].lastEnergy;
            float Nb = estimator.candidates[i].lastHarmonicRank;
            float rankEnergyTradeoff = Ea==Eb ? 0 : (Eb*Na-Ea*Nb)/(Ea-Eb);

            Text label(dec(candidate.f0)+" "_+dec(round(candidate.energy))
                       +" "_+dec(candidate.lastHarmonicRank)
                       +" "_+dec(round(rankEnergyTradeoff))
                       +" B~"_+dec(estimator.B?round(pow(estimator.B,-1./3)):0), 16,  vec4(color.xyz(),1.f));
            label.render(int2(position.x+size.x-label.sizeHint().x,position.y+16+(i)*48+32));
            for(uint n: range(candidate.lastHarmonicRank/*candidate.peaks.size*/)) {
                uint f = candidate.peaks[n];
                float x = this->x(f+0.5)*size.x;
                line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(color.xyz(),1.f/2));
                Text label(dec(n+1),16, vec4(color.xyz(),1.f));
                label.render(int2(position.x+x,position.y+16+(i)*48+(n%2)*16));
            }
            for(uint n: range(candidate.lastHarmonicRank/*candidate.peaksLS.size*/)) {
                uint f = candidate.peaksLS[n];
                float x = this->x(f+0.5)*size.x;
                //if(f!=candidate.peaks[n])
                    line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(color.xyz(),1.f/2));
            }
        }

        for(uint i=1;;i++) {
            uint f = round(expectedF*i);
            if(f>=spectrum.size) break;
            float x = this->x(f+0.5)*size.x;
            float v = i==1 ? 1 : min(1., 2 * (spectrum[f] ? (log2(spectrum[f]) - log2(sMin)) / (log2(sMax)-log2(sMin)) : 0));
            v = max(1.f/2, v);
            line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(0,0,1,v));
            Text label(dec(i),16,vec4(1,1,1,1));
            label.render(int2(position.x+x,position.y));
        }
    }
};

/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation {
    // Input
    const uint lowKey=parseKey(arguments()[0])-12, highKey=parseKey(arguments()[1])-12;
    Audio audio = decodeAudio("/Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_);
    ref<int32> stereo = audio.data;
    const uint rate = audio.rate;

    // Signal
    uint t=0;
    buffer<float> signal {N};

    // Analysis
    static constexpr uint N = 32768; // Analysis window size (A0 (27Hz~4K) * 2 (flat top window) * 2 (periods) * 2 (Nyquist))
    const uint periodSize = 4096;
    PitchEstimator estimator {N};

    // UI
    Plot plot {false, true, (float)N/rate, estimator.filteredSpectrum, estimator.spectrum, estimator};
    Window window {&plot, int2(1050, 1680/2), "Test"_};

    // Results
    int expectedKey = highKey+1;
    int previousKey = 0;
    int lastKey = highKey+1;
    uint success = 0, fail=0, tries = 0, total=0;
    Time totalTime;
    float maxB = 0;

    PitchEstimation() {
        assert_(rate==96000, audio.rate);
        assert_(audio.channels==2, audio.channels);
        assert_(((stereo.size/2+4*rate)/rate)/5>=uint(1+(highKey+1)-lowKey), uint(1+(highKey+1)-lowKey), (stereo.size/2/rate));

        window.backgroundColor=window.backgroundCenter=0; additiveBlend = true;
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(Key(' ')).connect(this, &PitchEstimation::next);
        window.frameReady.connect(this, &PitchEstimation::next);

        next();
    }

    void next() {
        if(fail) return;
        t+=periodSize;
        for(; t<=stereo.size/2-14*N; t+=periodSize) {
            const uint sync = highKey==(uint)parseKey("A6"_) ? rate/32 : 0; // Sync with benchmark
            // Checks for missed note
            if((t+sync+periodSize)/rate/5 != (t+sync)/rate/5 && lastKey != expectedKey) { fail++; log("False negative", strKey(expectedKey)); break; }

            // Prepares new period
            const int32* period = stereo + t*2;
            for(uint i: range(N-periodSize)) signal[i]=signal[i+periodSize];
            for(uint i: range(periodSize)) {
                float L = period[i*2+0];
                float x = L* 0x1p-24;
                signal[N-periodSize+i] = x;
            }

            // Benchmark
            if(t<5*rate) continue; // First 5 seconds are silence (let input settle, use for noise profile if necessary)
            expectedKey = highKey+1 - (t+sync)/rate/5; // Recorded one key every 5 seconds from high key to low key

            for(uint i: range(N)) estimator.windowed[i] = estimator.window[i] * signal[i];
            float f = estimator.estimate();

            const float confidenceThreshold = 1./10;//10 // Relative harmonic energy (i.e over current period energy)
            float confidence = estimator.harmonicEnergy  / estimator.periodEnergy;
            const float ambiguityThreshold = 1./7;//7 // 1- Energy of second candidate relative to first
            const float threshold = 1./17; //17

            if(confidence > confidenceThreshold/2) {
                float ambiguity = estimator.candidates.size==2 && estimator.candidates[1].key
                        && estimator.candidates[0].f0*(1+estimator.candidates[0].B)!=f ?
                            estimator.candidates[0].key / estimator.candidates[1].key : 0;
                assert_(ambiguity!=1, estimator.candidates[0].f0*(1+estimator.candidates[0].B), f);

                int key = round(pitchToKey(f*rate/N));
                float keyF0 = keyToPitch(key)*N/rate;
                const float offsetF0 = f > 0 ? 12*log2(f/keyF0) : 0;
                const float expectedF = keyToPitch(expectedKey)*N/rate;

                maxB = max(maxB, estimator.B);
                log(dec((t/rate)/60,2)+":"_+dec((t/rate)%60,2,'0')+"\t"_+strKey(expectedKey)+"\t"_+strKey(key)+"\t"_+dec(round(f*rate/N),4)+" Hz\t"_
                    +dec(round(100*offsetF0),2) +" c\t"_
                    +dec(round(confidence?1./confidence:0),2)+"\t"_+dec(round(1-ambiguity?1./(1-ambiguity):0),2)+"\t"_
                    +dec(round((confidence*(1-ambiguity))?1./(confidence*(1-ambiguity)):0),2)+"\t"_
                    +dec(round(1000*12*2*log2(1+3*(estimator.B>-1?estimator.B:0))))+" m\t"_
                    +(expectedKey == key ? (confidence > confidenceThreshold && 1-ambiguity > ambiguityThreshold ? "O"_ : "~"_) : "X"_)
                    //+"  "_+str((estimator.candidates[0].f0*(1+estimator.candidates[0].B))==f)+"\t"_
                    //+str(estimator.candidates.size,estimator.candidates[0].f0*(1+estimator.candidates[0].B),f)+"\t"_
                    +" "_+str(estimator.F1,"/",estimator.medianF0,"=",estimator.nHigh)
                    +" "_+str(round(expectedF))
                        //,"F0",estimator.medianF0)+" "_+str((float)estimator.F1/(estimator.nLow+1)-estimator.medianF0)+" "_
                    //+str(estimator.nLow)+"-"_+str(estimator.nHigh)+"\t"_
                    //+dec(round(estimator.candidates[0].energy))+" "_+dec(round(estimator.candidates[1].energy))+"\t"_
                    );

                if(confidence > confidenceThreshold && 1-ambiguity > ambiguityThreshold &&
                      confidence*(1-ambiguity) > threshold) {

                    if(expectedKey==key) {
                        success++;
                        lastKey = key;
                    }
                    else {
                        fail++;

                        if(t%(5*rate) > 4.5*rate && confidence<1./6 && expectedKey==parseKey("G4"_)) log("???"_); // Noise?
                        // FIXME: Inharmonic match on attack and release
                        else if(t%(5*rate) < rate/2 && key==expectedKey+1 && offsetF0<-1./3 && expectedKey==parseKey("A6"_)) log("! +"_); // Attack
                        //if(confidence<1./5 && t%(5*rate) > 4.5*rate && expectedKey=="G4"_) log("x -"_); // Release
                        else if(offsetF0>1./3 && key==expectedKey-1 && t%(5*rate) < 1*rate && expectedKey==parseKey("C3"_)) log("! -"_); // Attack
                        else if(confidence<1./7 && offsetF0>0 && key==expectedKey-1 && t%(5*rate) < 1*rate && expectedKey==parseKey("A2"_)) log("! -"_); // Attack
                        else if(confidence<1./6 && key==expectedKey-1 && t%(5*rate) < 1*rate && expectedKey==parseKey("G#2"_)) log("! -"_); // Attack
                        else if(confidence<1./5 && key==expectedKey+1 && t%(5*rate) < rate/2 && expectedKey==parseKey("G2"_)) log("! +"_); // Attack
                        else if(key==expectedKey-1 && offsetF0>1./3 && t%(5*rate) <= rate && expectedKey==parseKey("G2"_)) log("! -"_); // Attack
                        else if(key==expectedKey-1 && confidence<1./7 && t%(5*rate) <= rate/2 && expectedKey==parseKey("F#2"_)) log("! -"_); // Attack
                        else if(offsetF0>1./6 && key==expectedKey-1 && confidence<1./4 && expectedKey==parseKey("C2"_)) log("x -"_); // Mistune?
                        else if(confidence<1./4 && key==expectedKey+1 && t%(5*rate) < rate/2 && expectedKey==parseKey("A#1"_)) log("! +"_); // Attack
                        else if(offsetF0>1./3 && key==expectedKey-1 && t%(5*rate) < rate/2 && expectedKey==parseKey("A#1"_)) log("! -"_); // Attack
                        else if(offsetF0>1./3 && key==expectedKey-1 && t%(5*rate) < 2*rate && expectedKey==parseKey("A1"_)) log("! -"_); // Mistune?
                        else if(offsetF0>1./7 && key==expectedKey-1 && t%(5*rate) > 4*rate && expectedKey==parseKey("E1"_)) log("x -"_); // Release
                        else if(confidence<1./5 && key==expectedKey-1 && t%(5*rate) < rate/2 && expectedKey==parseKey("D#1"_)) log("! -"_); // Attack
                        else if(offsetF0>1./3 && key==expectedKey-1 && t%(5*rate) > 4*rate && expectedKey==parseKey("D#1"_)) log("x -"_); // Release
                        else if(offsetF0>1./3 && key==expectedKey-1 && t%(5*rate) > 3*rate && expectedKey==parseKey("C1"_)) log("x -"_); // Release
                        else if(offsetF0>1./4 && key==expectedKey-1 && confidence<1./3 && expectedKey==parseKey("A#0"_)) log("x -"_); // Mistune?
                        else if(offsetF0>1./4 && key==expectedKey-1 && t%(5*rate) < rate && expectedKey==parseKey("F#0"_)) log("x -"_); // Mistune?
                        else if(offsetF0>0 && key==expectedKey-1 && t%(5*rate) > 3*rate && expectedKey==parseKey("B-1"_)) log("-"_); // Mistune?
                        else if(offsetF0>0 && key==expectedKey-1 && confidence<1./3 && expectedKey==parseKey("A#-1"_)) {
                            if(offsetF0>1./3) { lastKey = expectedKey; log("FIXME"_); }
                            log("-"_); // Mistune? (FIXME)
                        }
                        else if(t%(5*rate) < rate/2 && confidence<1./4 && expectedKey==parseKey("A#-1"_)) log("!"_); // Attack
                        else {
                            plot.expectedF = expectedF;
                            plot.iMin = min(f, expectedF)/2;
                            plot.iMin = min(estimator.minF, uint(f));
                            plot.iMin = 0;
                            //plot.iMin = max(plot.iMin, estimator.F1/2);
                            plot.iMax = expectedF;
                            plot.iMax = max(plot.iMax, estimator.maxF);
                            if(estimator.candidates.size) plot.iMax = max(plot.iMax, uint(estimator.candidates.last().f0 * (estimator.candidates.last().lastHarmonicRank+1)));
                            if(estimator.candidates.size) plot.iMax = max(plot.iMax, uint(estimator.candidates[0].f0 * (estimator.candidates[0].lastHarmonicRank+1)));
                            plot.iMax = max(plot.iMax, estimator.F1*8);
                            plot.iMax = max(plot.iMax, uint(expectedF*8));
                            plot.iMax = min(plot.iMax, uint(expectedF*estimator.lastHarmonicRank));
                            plot.iMax = min(plot.iMax, estimator.fMax);

                            log("FIXME", confidence<1./5, float(t%(5*rate))/rate);
                            break;
                        }
                    }
                    tries++;
                }
                previousKey = key;
            }
            total++;
        }
        if(plot.expectedF) {
            window.setTitle(strKey(expectedKey));
            window.render();
            window.show();
        } else log(100*12*2*log2(1+3*(estimator.B>-1?estimator.B:0)));
    }
} app;
