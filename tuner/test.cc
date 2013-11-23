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
#include "biquad.h"
#include <fftw3.h> //fftw3f

int parseKey(const string& value) {
    int note=24;
    uint i=0;
    assert(toLower(value[i])>='a' && toLower(value[i])<='g');
    note += "c#d#ef#g#a#b"_.indexOf(toLower(value[i]));
    i++;
    if(value.size==3) {
        if(value[i]=='#') { note++; i++; }
        else error(value);
    }
    assert(value[i]>='0' && value[i]<='8', value);
    note += 12*(value[i]-'0');
    return note;
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
    const PitchEstimator& estimator;
    const bool logx;
    const bool logy;
    const float scale;
    uint iMin = 0, iMax = 0;
    ref<float> data;
    float expectedF = 0, estimatedF = 0;

    Plot(const PitchEstimator& estimator, bool logx, bool logy, float scale):
        estimator(estimator),logx(logx),logy(logy),scale(scale){}
    float x(float f) { return logx ? (log2(f)-log2((float)iMin))/(log2((float)iMax)-log2((float)iMin)) : (float)(f-iMin)/(iMax-iMin); }
    float s(uint f) { return data[f]; }
    void render(int2 position, int2 size) {
        assert_(/*0 <= iMi0 &&*/ iMin <= iMax && iMax <= data.size, iMin, iMax, data.size);
        float sMax = -inf; for(uint i: range(iMin, iMax)) sMax = max(sMax, s(i));
        float sMin = estimator.periodPower;
        float noiseThreshold = 2*estimator.periodPower;

        { // Noise threshold
            float s = noiseThreshold;
            float y = (logy ? (log2(s) - log2(sMin)) / (log2(sMax)-log2(sMin)) : (s / sMax)) * (size.y-12);
            line(position.x,position.y+size.y-y-0.5,position.x+size.x,position.y+size.y-y+0.5,vec4(1,0,0,1));
        }

        for(uint i: range(iMin, iMax)) { // Energy density
            float x0 = x(i) * size.x;
            float x1 = x(i+1) * size.x;
            float y = (logy ? (s(i) ? (log2(s(i)) - log2(sMin)) / (log2(sMax)-log2(sMin)) : 0) : (s(i) / sMax)) * (size.y-12);
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,vec4(1,1,1,0.5));
        }

        // First peak and first F0 estimation (median distance)
        {float x = this->x(estimator.firstPeakFrequency+0.5)*size.x;
            fill(position.x+floor(x),position.y,position.x+ceil(x),position.y+size.y, vec4(0,1,0,1./2));}
        {float x = this->x(estimator.firstPeakFrequency+estimator.medianF0+0.5)*size.x;
            fill(position.x+floor(x),position.y,position.x+ceil(x),position.y+size.y, vec4(0,1,0,1./3));}
        {float x = this->x(estimator.firstPeakFrequency-estimator.medianF0+0.5)*size.x;
            fill(position.x+floor(x),position.y,position.x+ceil(x),position.y+size.y, vec4(0,1,0,1./3));}

        for(uint i: range(estimator.candidates.size)) {
            const auto& candidate = estimator.candidates[i];
            bool best = i==estimator.candidates.size-1; //estimator.leastSquareF0[i]==estimator.bestF0;
            vec4 color(!best,best,0,1); //1./(2*(last-(2*t+i-1)))
            Text label(ftoa(candidate.key)+" "_+ftoa(candidate.B?1./candidate.B:0), 16,  vec4(color.xyz(),1.f));
            label.render(int2(position.x+size.x-label.sizeHint().x,position.y+16+(i)*32));
            for(uint n: range(candidate.peaks.size)) {
                uint f = candidate.peaks[n];
                float x = this->x(f+0.5)*size.x;
                line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(color.xyz(),1.f/2));

                {uint fLS = candidate.peaksLS[n];
                float x = this->x(fLS+0.5)*size.x;
                line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(color.xyz(),1.f));}

                Text label(dec(n+1)+" "_+ftoa(f/candidate.f0,1),16, vec4(color.xyz(),1.f));
                label.render(int2(position.x+x,position.y+16+(i)*32+(n%2)*16));
            }
        }

        for(uint i=64; i>=1; i--) {
            float f = expectedF*i;
            float x = this->x(round(f)+0.5)*size.x;
            float v = i==1 ? 1 : min(1., 2 * (log2(s(f)) - log2(sMin)) / (log2(sMax)-log2(sMin)));
            v = max(1.f/2, v);
            line(position.x+x,position.y,position.x+x+1,position.y+size.y, vec4(0,0,1,v));
            Text label(dec(i),16,vec4(1,1,1,1));
            label.render(int2(position.x+x,position.y));
        }

        {
            const uint N = data.size*2;
            const uint rate = 96000;
            {
                const float bandwidth = 1./(1*12);
                float x0 = this->x(50.*N/rate*exp2(-bandwidth))*size.x;
                float x1 = this->x(50.*N/rate*exp2(+bandwidth))*size.x;
                fill(position.x+floor(x0),position.y,position.x+ceil(x1),position.y+size.y, vec4(1,1,0,1./2));
            }
            {
                const float bandwidth = 1./(3*12);
                float x0 = this->x(3*50.*N/rate*exp2(-bandwidth))*size.x;
                float x1 = this->x(3*50.*N/rate*exp2(+bandwidth))*size.x;
                fill(position.x+floor(x0),position.y,position.x+ceil(x1),position.y+size.y, vec4(1,1,0,1./3));
            }
        }
    }
};

/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation {
    // Input
    const uint lowKey=parseKey("A0"_)-12, highKey=parseKey("B2"_)-12;
    //const uint lowKey=parseKey("F3"_)-12, highKey=parseKey("F5"_)-12;
    Audio audio = decodeAudio("/Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_);
    ref<int32> stereo = audio.data;
    const uint rate = audio.rate;

    // Signal
    uint t=0;
    float signalMaximum = 0;
    buffer<float> signal {N};

    // Analysis
    static constexpr uint N = 32768; // Analysis window size (A0 (27Hz~4K) * 2 (flat top window) * 2 (periods) * 2 (Nyquist))
    const uint periodSize = 4096;
    PitchEstimator estimator {N};
    const float fMin  = N*440*exp2(-4)/rate; // A0
    HighPass highPass {3*50./rate}; // High pass filter to remove low frequency noise
    Notch notch1 {1*50./rate, 1./(1*12)}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./(3*12)}; // Notch filter to remove 150Hz noise

    // UI
    Plot spectrum {estimator, false, true, (float)rate/N};
    OffsetPlot profile;
    VBox plots {{&spectrum/*, &profile*/}};
    Window window {&plots, int2(1050, 1680/2), "Test"_};

    // Results
    int expectedKey = highKey+1;
    int previousKey = 0;
    int lastKey = highKey+1;
    uint success = 0, fail=0, tries = 0, total=0;
    float minAbsolute=-inf, minAllowAbsolute = -inf, maxDenyAbsolute = -inf, globalMaxDenyAbsolute = inf, maxAbsolute = -inf;
    float minRelative=-inf, minAllowRelative = -inf, maxDenyRelative = -inf, globalMaxDenyRelative = inf, maxRelative= -inf;
    Time totalTime;
    float maxB = 0;

    PitchEstimation() {
        assert_(rate==96000, audio.rate);
        assert_(audio.channels==2, audio.channels);
        assert_(((stereo.size/2+rate/2)/rate+3)/5==uint(1+(highKey+1)-lowKey));
        for(int i: range(stereo.size/2)) signalMaximum=::max(signalMaximum, abs(float(stereo[i*2+0])+float(stereo[i*2+1])));
        assert_(signalMaximum<exp2(32));

        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(Key(' ')).connect(this, &PitchEstimation::next);
        window.frameReady.connect(this, &PitchEstimation::next);

        profile.reset();
        next();
    }

    void next() {
        if(fail) return;
        t+=periodSize;
        for(; t<=stereo.size/2-N; t+=periodSize) {
            // Checks for missed note
            if((t+periodSize)/rate/5 != t/rate/5) {
                if(maxDenyAbsolute!=-inf) globalMaxDenyAbsolute = min(globalMaxDenyAbsolute, maxDenyAbsolute); maxDenyAbsolute = -inf;
                if(maxDenyRelative!=-inf) globalMaxDenyRelative = min(globalMaxDenyRelative, maxDenyRelative); maxDenyRelative = -inf;
                if(lastKey != expectedKey) { fail++; log("False negative", strKey(expectedKey)); break; }
                maxAbsolute=-inf, maxRelative=-inf;
            }

            // Prepares new period
            const int32* period = stereo + t*2;
            for(uint i: range(N-periodSize)) signal[i]=signal[i+periodSize];
            for(uint i: range(periodSize)) {
                float L = period[i*2+0], R = period[i*2+1];
                float x = (L+R) / signalMaximum;
                x = highPass(x);
                x = notch1(x);
                x = notch3(x);
                signal[N-periodSize+i] = x;
            }

            // Benchmark
            if(t<5*rate) continue; // First 5 seconds are silence (let input settle, use for noise profile if necessary)
            expectedKey = highKey+1 - t/rate/5; // Recorded one key every 5 seconds from high key to low key

            float f0 = estimator.estimate(signal, fMin);
            //assert_(f0==0 || pitchToKey(f0*rate/N)>-7, fMin, pitchToKey(f0*rate/N), 1./estimator.inharmonicity);
            int key = f0>1 ? round(pitchToKey(f0*rate/N)) : 0; //FIXME: stretched reference

            const float absoluteThreshold = 1./2; // Absolute harmonic energy in the current period (i.e over mean period energy)
            //float meanPeriodEnergy = estimator.meanPeriodEnergy; // Stabilizes around 4 (depends on FFT size, energy, range)
            float meanPeriodEnergy = 4; // Using constant to benchmark before mean energy converges
            float absolute = estimator.harmonicEnergy / meanPeriodEnergy;

            const float relativeThreshold = 1./7; // Relative harmonic energy (i.e over current period energy)
            float periodEnergy = estimator.periodEnergy;
            float relative = estimator.harmonicEnergy  / periodEnergy;

            float keyF0 = keyToPitch(key)*N/rate;
            const float offsetF0 = f0 ? 12*log2(f0/keyF0) : 0;
            //float keyPeakF = keyToPitch(round(pitchToKey(rate*estimator.fPeak/N)))*N/rate;
            //const float offsetPeak = estimator.fPeak ?  12*log2(estimator.fPeak/keyPeakF) : 0;
            //const float fError = 12*log2((keyF+1./PitchEstimator::H)/keyF);
            const float expectedF = keyToPitch(expectedKey)*N/rate;

            /*const auto& candidates = estimator.candidates.slice(estimator.candidates.size-2);
            uint first = (candidates[0].H+estimator.peakCountRatioTradeoff)*candidates[1].peakCount;
            uint second = (candidates[1].H+estimator.peakCountRatioTradeoff)*candidates[0].peakCount;*/

            /*uint bestPeakCount=0, bestH=-1;
            for(uint n=1,lastH=0, peakCount=0;;n++) {
                const uint f = round(expectedF*n);
                if(f>=N/2) break;
                if(estimator.spectrum[f] > 2*estimator.periodPower) {
                    lastH = n; peakCount++;
                    if((bestH+estimator.peakCountRatioTradeoff)*peakCount >= (lastH+estimator.peakCountRatioTradeoff)*bestPeakCount) {
                        bestH=lastH, bestPeakCount=peakCount;
                    }
                }
            }*/

            log(dec((t/rate)/60,2)+":"_+ftoa(float(t%(60*rate))/rate,2,2)+"\t"_+strKey(expectedKey)+"\t"_+strKey(key)+"\t"_
                +dec(round(f0*rate/N),4)+" Hz\t"_ +dec(round(100*offsetF0),2) +" c\t"_ //dec(round(100*offsetPeak),) +"\t"_
                +dec(round(absolute?1./(absolute):0),2)+" "_+dec(round(relative?1./relative:0),2)+"\t"_//+dec(estimator.peaks.size)+"\t"_
                //+dec(estimator.peakCount,2)+" / "_+dec(estimator.lastHarmonicRank,2)+"\t"_
                //+dec(bestPeakCount,2)+" / "_+dec(bestH,2)+"\t"_
                +"B~"_+dec(estimator.B?1./estimator.B:0,3)+"\t"_
                //+"1st: "_+str(first)+" 2nd: "_+str(second)+"\t"_
                +(expectedKey == key ? (f0 > fMin && absolute > absoluteThreshold && relative > relativeThreshold ? "O"_ : "~"_) : "X"_));

            if(/*f0 > fMin &&*/ absolute > absoluteThreshold && relative > relativeThreshold) {

                if(expectedKey==key) {
                    //if(estimator.inharmonicity>maxB) log(1./(maxB=estimator.inharmonicity));
                    success++;
                    lastKey = key;

                    assert_(key>=21 && key<21+keyCount, f0);
                    float& keyOffset = profile.offsets[key-21];
                    {const float alpha = 1./8; keyOffset = (1-alpha)*keyOffset + alpha*offsetF0;} // Smoothes offset changes (~1sec)
                    float variance = sq(offsetF0 - keyOffset);
                    float& keyVariance = profile.variances[key-21];
                    {const float alpha = 1./8; keyVariance = (1-alpha)*keyVariance + alpha*variance;} // Smoothes deviation changes
                }
                else {
                    fail++;

                    spectrum.data = estimator.spectrum;
                    spectrum.iMin = min(f0, expectedF);
                    spectrum.estimatedF = f0;
                    spectrum.expectedF = expectedF;
                    //spectrum.iMax = max(f0,expectedF)*max(estimator.lastHarmonicRank,bestH);
                    //spectrum.iMax = max(uint(16*max(f0,expectedF)), estimator.bestPeaks.last()+1);
                    //spectrum.iMax = max(f0,expectedF)*estimator.lastHarmonicRank;
                    spectrum.iMax = estimator.lastPeak+8*f0;

                    // Relax for hard cases
                    if(     relative<1./2 &&
                            (
                                ((offsetF0>1./5 || expectedKey<=parseKey("G#1"_)) && key==expectedKey-1)
                                || (t%(5*rate)<2*rate && ((offsetF0>-1./3 && key==expectedKey-1) || t%(5*rate)<rate))
                              || (t%(5*rate)>4*rate && key<expectedKey)
                             || ((previousKey==expectedKey || previousKey==expectedKey-1 ||
                                  key==expectedKey+1 || key==expectedKey+2 || key==expectedKey+3)
                                 && expectedKey<=parseKey("A#0"_))
                             || (t%(5*rate)<2*rate && previousKey==expectedKey && relative<1./3 /*&& key==expectedKey-12*/))) {
                        if(0) {}
                        else if(offsetF0>3./8 && key==expectedKey-1 && apply(split("A2 B1 A#1 A1 F1 E1"_), parseKey).contains(expectedKey)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>3./8 && key==expectedKey-1 && expectedKey<=parseKey("D1"_)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>2./8 && key==expectedKey-1 && apply(split("E1 C#1 C1 B0 A#0 G0 F0"_), parseKey).contains(expectedKey)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>0 && key==expectedKey-1 && expectedKey<=parseKey("C#1"_)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>-1./4 && key==expectedKey-1 && expectedKey<=parseKey("G0"_)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>2./8 && key==expectedKey-1 && apply(split("G#1"_), parseKey).contains(expectedKey)) log("-"_);
                        else if(offsetF0>1./8 && key==expectedKey-1 && apply(split("G1 F#1"_), parseKey).contains(expectedKey)) log("-"_);
                        else if(offsetF0>0 && key==expectedKey-1 && apply(split("F1 E1 D#1 D1 C#1"_), parseKey).contains(expectedKey)) log("-"_);
                        else if(key==expectedKey-1 && expectedKey<=parseKey("C1"_)) log("-"_);
                        else if(t%(5*rate) < 2*rate && relative<1./3 && apply(split("C4 A3"_), parseKey).contains(expectedKey)) log("/"_);
                        else if((relative<1./3 && (t%(5*rate) < rate)) || (t%(5*rate) < rate/2)) log("!"_); // Attack
                        else if(t%(5*rate)>4*rate && key<=expectedKey) log("."_); // Release
                        else if(expectedKey<=parseKey("A#0"_) && key==expectedKey+1) log("~"_); // Bass strings swings
                        else if(expectedKey<=parseKey("F#0"_) && key==expectedKey-2) log("_"_); // Bass strings swings
                        else if(expectedKey<=parseKey("E0"_) && key==expectedKey+2) log("_"_); // Bass strings swings
                        else { log("Corner case"); break; }
                    } else { log("FIXME",relative<1./2, key==expectedKey-1, expectedKey<=parseKey("D#0"_) ); break; }
                }
                tries++;
            }
            previousKey = key;
            total++;

            if(key==expectedKey /*&& lastKey!=expectedKey*/) {
                maxAbsolute = max(maxAbsolute, absolute); if(relative > relativeThreshold) maxDenyAbsolute = max(maxDenyAbsolute, absolute);
                maxRelative = max(maxRelative, relative); if(absolute > absoluteThreshold) maxDenyRelative = max(maxDenyRelative, relative);
            } else {
                minAbsolute = max(minAbsolute, absolute); if(relative > relativeThreshold) minAllowAbsolute = max(minAllowAbsolute, absolute);
                minRelative = max(minRelative, relative); if(absolute > absoluteThreshold) minAllowRelative = max(minAllowRelative, relative);
            }

            //break;
        }
        if(fail) {
            log(str(1./minAbsolute)+" "_+str(1./minAllowAbsolute)+" "_+str(1./globalMaxDenyAbsolute)+" "_+str(1./maxDenyAbsolute)+" "_+str(1./maxAbsolute));
            log(str(1./minRelative)+" "_+str(1./minAllowRelative)+" "_+str(1./globalMaxDenyRelative)+" "_+str(1./maxDenyRelative)+" "_+str(1./maxRelative));
        } else {
            log("Maximum inharmonicity", maxB?1./maxB:0);
            log("Mean period energy", log2(estimator.meanPeriodEnergy));
        }
        if(spectrum.data) {
            window.setTitle(strKey(expectedKey));
            window.render();
            window.show();
        }
    }
} app;
