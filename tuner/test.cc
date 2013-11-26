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
    const bool logx, logy; // Whether to use log scale on x/y axis
    const float resolution; // Resolution in bins / Hz
    uint iMin = 0, iMax = 0; // Displayed range in bins
    ref<float> spectrum; // Displayed power spectrum
    // Additional analysis data
    const PitchEstimator& estimator;
    float expectedF = 0;

    Plot(bool logx, bool logy, float resolution, ref<float> spectrum, const PitchEstimator& estimator):
        logx(logx),logy(logy),resolution(resolution),spectrum(spectrum),estimator(estimator){}
    float x(float f) { return logx ? (log2(f)-log2((float)iMin))/(log2((float)iMax)-log2((float)iMin)) : (float)(f-iMin)/(iMax-iMin); }
    void render(int2 position, int2 size) {
        assert_(iMin < iMax && iMax <= spectrum.size, iMin, iMax);
        if(iMin >= iMax) return;
        float sMax = -inf; for(uint i: range(iMin, iMax)) sMax = max(sMax, spectrum[i]);
        float sMin = min(estimator.periodPower,  estimator.noiseThreshold/2);

        {float s = estimator.noiseThreshold;
        float y = (logy ? (log2(s) - log2(sMin)) / (log2(sMax)-log2(sMin)) : (s / sMax)) * (size.y-12);
        line(position.x,position.y+size.y-y-0.5,position.x+size.x,position.y+size.y-y+0.5,vec4(0,1,0,1));}

        for(uint i: range(iMin, iMax)) { // Energy density
            float x0 = x(i) * size.x;
            float x1 = x(i+1) * size.x;
            float y = (logy ? (spectrum[i] ? (log2(spectrum[i]) - log2(sMin)) / (log2(sMax)-log2(sMin)) : 0) : (spectrum[i] / sMax)) * (size.y-12);
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,vec4(1,1,1,1));
        }

        /*for(uint f: estimator.peaks) {
            float x = this->x(f+0.5)*size.x;
            line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(1,0,0,1./4));
        }*/

        // First peak and first F0 estimation (median distance)
        {float x = this->x(estimator.firstPeakFrequency+0.5)*size.x;
            fill(position.x+floor(x),position.y,position.x+ceil(x),position.y+size.y, vec4(0,1,0,1));}
        {float x = this->x(estimator.firstPeakFrequency+estimator.medianF0+0.5)*size.x;
            fill(position.x+floor(x),position.y,position.x+ceil(x),position.y+size.y, vec4(0,1,0,1./2));}
        {float x = this->x(estimator.firstPeakFrequency-estimator.medianF0+0.5)*size.x;
            fill(position.x+floor(x),position.y,position.x+ceil(x),position.y+size.y, vec4(0,1,0,1./2));}

        for(uint i: range(estimator.candidates.size)) {
            const auto& candidate = estimator.candidates[i];
            bool best = i==estimator.candidates.size-1;
            vec4 color(!best,best,0,1);
            float Ea = estimator.candidates.last().energy;
            float Na = estimator.candidates.last().peaks.size;
            float Eb = estimator.candidates[0].energy;
            float Nb = estimator.candidates[0].peaks.size;
            float rankEnergyTradeoff = (Eb*Na-Ea*Nb)/(Ea-Eb);
            Text label(ftoa(candidate.energy)+" "_+ftoa(candidate.B?1./candidate.B:0)+" "_+dec(candidate.peaks.size)+" "_+ftoa(rankEnergyTradeoff),
                       16,  vec4(color.xyz(),1.f));
            label.render(int2(position.x+size.x-label.sizeHint().x,position.y+16+(i)*48+32));
            for(uint n: range(candidate.peaks.size)) {
                uint f = candidate.peaks[n];
                float x = this->x(f+0.5)*size.x;
                line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(color.xyz(),1.f/2));

                Text label(dec(n+1) /*+" "_+ftoa(spectrum[f])*/
                           ,16, vec4(color.xyz(),1.f));
                label.render(int2(position.x+x,position.y+16+(i)*48+(n%2)*16));
            }
            for(uint n: range(candidate.peaksLS.size)) {
                uint f = candidate.peaksLS[n];
                float x = this->x(f+0.5)*size.x;
                line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(color.xyz(),1.f));
            }
        }

        for(uint i=1;;i++) {
            uint f = round(expectedF*i);
            if(f>=spectrum.size) break;
            float x = this->x(f+0.5)*size.x;
            float v = i==1 ? 1 : min(1., 2 * (log2(spectrum[f]) - log2(sMin)) / (log2(sMax)-log2(sMin)));
            v = max(1.f/2, v);
            line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(0,0,1,v));
            Text label(dec(i),16,vec4(1,1,1,1));
            label.render(int2(position.x+x,position.y));
        }

        for(uint i=1; i<=5; i+=2){
            const float bandwidth = 1./(i*12);
            float x0 = this->x(i*50.*resolution*exp2(-bandwidth))*size.x;
            float x1 = this->x(i*50.*resolution*exp2(+bandwidth))*size.x;
            fill(position.x+floor(x0),position.y,position.x+ceil(x1),position.y+size.y, vec4(1,1,0,1./2));
        }
    }
};

/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation {
    // Input
    //const uint lowKey=parseKey("A0"_)-12, highKey=parseKey("C2"_)-12;
    //const uint lowKey=parseKey("A0"_)-12, highKey=parseKey("B2"_)-12;
    //const uint lowKey=parseKey("B1"_)-12, highKey=parseKey("D3"_)-12;
    //const uint lowKey=parseKey("F3"_)-12, highKey=parseKey("F5"_)-12;
    const uint lowKey=parseKey("A6"_)-12, highKey=parseKey("A7"_)-12;
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
    HighPass highPass {50./rate}; // High pass filter to remove low frequency noise
    Notch notch1 {1*50./rate, 2./(1*12)}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./(3*12)}; // Notch filter to remove 150Hz noise
    Notch notch5 {5*50./rate, 1./(5*12)}; // Notch filter to remove 250Hz noise

    // UI
    Plot spectrum {false, true, (float)N/rate, estimator.spectrum, estimator};
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
        assert_(((stereo.size/2+4*rate)/rate)/5>=uint(1+(highKey+1)-lowKey), uint(1+(highKey+1)-lowKey), (stereo.size/2/rate));
        for(int i: range(stereo.size/2)) signalMaximum=::max(signalMaximum, abs(float(stereo[i*2+0])+float(stereo[i*2+1])));
        log(log2(signalMaximum));
        assert_(signalMaximum<exp2(31));

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
        for(; t<=stereo.size/2-14*N; t+=periodSize) {
            const uint sync = rate; // Sync with benchmark
            // Checks for missed note
            if((t+sync+periodSize)/rate/5 != (t+sync)/rate/5) {
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
                //signalMaximum=::max(signalMaximum, abs(L+R));
                assert_(abs(L+R) <= signalMaximum);
                float x = (L+R) / signalMaximum;
                x = highPass(x);
                x = notch1(x);
                x = notch3(x);
                //x = notch5(x);
                signal[N-periodSize+i] = x;
            }

            // Benchmark
            if(t<5*rate) continue; // First 5 seconds are silence (let input settle, use for noise profile if necessary)
            expectedKey = highKey+1 - (t+sync)/rate/5; // Recorded one key every 5 seconds from high key to low key

            float f = estimator.estimate(signal);
            const float expectedF = keyToPitch(expectedKey)*N/rate;
            /*if(f0 <= 0) {
                log(f0);
                spectrum.expectedF = expectedF;
                spectrum.iMin = min(estimator.peaks.first(), uint(expectedF))-2;
                spectrum.iMax = max(estimator.peaks.last(), estimator.lastPeak)+2;
                fail++;
                break;
            }*/

            const float absoluteThreshold = 1./13; // Absolute harmonic energy in the current period (i.e over mean period energy)
            float meanPeriodEnergy = 64; // Fix for benchmark start
            float absolute = estimator.harmonicEnergy / meanPeriodEnergy;

            const float relativeThreshold = 1./13; // Relative harmonic energy (i.e over current period energy)
            float periodEnergy = estimator.periodEnergy;
            float relative = estimator.harmonicEnergy  / periodEnergy;

            int key = f>1 ? round(pitchToKey(f*rate/N)) : 0; //FIXME: stretched reference
            float keyF0 = keyToPitch(key)*N/rate;
            const float offsetF0 = f > 0 ? 12*log2(f/keyF0) : 0;
            const float offsetFit = estimator.F0*(1+estimator.B) > 0 ? 12*log2(estimator.F0*(1+estimator.B)/keyF0) : 0;
            const float offsetEnergy = estimator.energyWeightedF > 0 ? 12*log2(estimator.energyWeightedF/keyF0) : 0;
            const float offsetPeak = estimator.peakF > 0 ? 12*log2(estimator.peakF/keyF0) : 0;

            log(dec((t/rate)/60,2)+":"_+dec((t/rate)%60,2,'0')+"\t"_+strKey(expectedKey)+"\t"_+strKey(key)+"\t"_+dec(round(f*rate/N),4)+" Hz\t"_
                +dec(round(100*offsetFit),2) +" c\t"_+dec(round(100*offsetEnergy)) +" c\t"_+dec(round(100*offsetPeak)) +" c\t"_
                +dec(round(absolute?1./(absolute):0),2)+" "_+dec(round(relative?1./relative:0),2)+"\t"_
                +"B~"_+dec(round(100*12*log2(1+(estimator.B>-1?estimator.B:0))))+" c\t"_
                +(expectedKey == key ? (absolute > absoluteThreshold && relative > relativeThreshold ? "O"_ : "~"_) : "X"_));

            if(absolute > absoluteThreshold && relative > relativeThreshold) {

                if(expectedKey==key) {
                    success++;
                    lastKey = key;

                    assert_(key>=21 && key<21+keyCount, f);
                    float& keyOffset = profile.offsets[key-21];
                    {const float alpha = 1./4; keyOffset = (1-alpha)*keyOffset + alpha*offsetF0;} // Smoothes offset changes
                    float variance = sq(offsetF0 - keyOffset);
                    float& keyVariance = profile.variances[key-21];
                    {const float alpha = 1./4; keyVariance = (1-alpha)*keyVariance + alpha*variance;} // Smoothes deviation changes
                }
                else {
                    fail++;

                    spectrum.expectedF = expectedF;
                    spectrum.iMin = min(estimator.peaks.first(), (uint)min(f, expectedF))-2;
                    spectrum.iMax = max(estimator.peaks.last(), estimator.lastPeak)+2;

                    // Relax for hard cases
                    if(     relative<1./2 &&
                            (
                                ((offsetF0>1./5 || expectedKey<=parseKey("G#1"_)) && key==expectedKey-1)
                                || (t%(5*rate)<2*rate && ((offsetF0>-1./3 && key==expectedKey-1) || t%(5*rate)<rate))
                              || (t%(5*rate)>4*rate && key<expectedKey)
                             || ((previousKey==expectedKey || previousKey==expectedKey-1 || key==expectedKey-2 ||
                                  key==expectedKey+1 || key==expectedKey+2 || key==expectedKey+3)
                                 && expectedKey<=parseKey("A#0"_))
                             || (t%(5*rate)<2*rate && previousKey==expectedKey && relative<1./3)
                                || (offsetF0<0 && key==expectedKey+1)
                                )
                            ) {
                        if(0) {}
                        else if(offsetF0>3./8 && key==expectedKey-1 && apply(split("A2 D2 B1 A#1 A1"_), parseKey).contains(expectedKey)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>2./8 && key==expectedKey-1 && apply(split("D2"_), parseKey).contains(expectedKey)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>2./8 && key==expectedKey-1 && expectedKey<=parseKey("F1"_)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>-2./8 && key==expectedKey-1 && expectedKey<=parseKey("C#1"_)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>-3./8 && key==expectedKey-1 && expectedKey<=parseKey("F#0"_)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if( key==expectedKey-1 && expectedKey<=parseKey("F#0"_)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>2./8 && key==expectedKey-1 && apply(split("G#1"_), parseKey).contains(expectedKey)) log("-"_);
                        else if(offsetF0>1./8 && key==expectedKey-1 && apply(split("G1 F#1"_), parseKey).contains(expectedKey)) log("-"_);
                        else if(offsetF0>0 && key==expectedKey-1 && apply(split("F1 E1 D#1 D1 C#1"_), parseKey).contains(expectedKey)) log("-"_);
                        else if(key==expectedKey-1 && expectedKey<=parseKey("C1"_)) log("-"_);
                        else if(t%(5*rate) < 2*rate && relative<1./3 && apply(split("C4 A3"_), parseKey).contains(expectedKey)) log("/"_);
                        //else if((relative<1./3 && (t%(5*rate) < rate)) || (t%(5*rate) < rate/2) && key==expectedKey+1) log("!"_); // Attack
                        //else if((relative<1./3 && (t%(5*rate) < rate)) || (t%(5*rate) < rate/2)) log("!"_); // Attack
                        else if(t%(5*rate)>4*rate && key<=expectedKey) log("."_); // Release
                        //else if(expectedKey<=parseKey("A#0"_) && key==expectedKey+1 && offsetF0<0) log("~"_); // Bass strings swings
                        else if(expectedKey<=parseKey("F#0"_) && key==expectedKey-2) log("_"_); // Bass strings swings
                        else if(expectedKey<=parseKey("F0"_) && key==expectedKey+2) log("_"_); // Bass strings swings
                        else { log("Corner case", stereo.size/2-t, (stereo.size/2-t)/N); break; }
                    } else { log("FIXME",relative<1./2, key==expectedKey-1, expectedKey<=parseKey("D#0"_) ); break; }
                }
                tries++;
            } else if((key==expectedKey || key==expectedKey-1) && expectedKey<=parseKey("C0"_)) { // FIXME: Hard keys
                log("?"_); lastKey=expectedKey; // Any match (even without confidence) will allow the key to pass (but false positives do still abort)
            }
            previousKey = key;
            total++;

            if(key==expectedKey) {
                maxAbsolute = max(maxAbsolute, absolute); if(relative > relativeThreshold) maxDenyAbsolute = max(maxDenyAbsolute, absolute);
                maxRelative = max(maxRelative, relative); if(absolute > absoluteThreshold) maxDenyRelative = max(maxDenyRelative, relative);
            } else {
                minAbsolute = max(minAbsolute, absolute); if(relative > relativeThreshold) minAllowAbsolute = max(minAllowAbsolute, absolute);
                minRelative = max(minRelative, relative); if(absolute > absoluteThreshold) minAllowRelative = max(minAllowRelative, relative);
            }
        }
        if(spectrum.expectedF) {
            window.setTitle(strKey(expectedKey));
            window.render();
            window.show();
        }
    }
} app;
