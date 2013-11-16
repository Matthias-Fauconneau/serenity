#include "thread.h"
#include "math.h"
#include "pitch.h"
#include <fftw3.h> //fftw3f
#include "ffmpeg.h"
#include "data.h"
#include "time.h"
#include "layout.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "profile.h"

uint parseKey(const string& value) {
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
    const bool harmonic;
    const bool logx;
    const bool logy;
    const float scale;
    uint iMin = 0, iMax = 0;
    ref<float> data;
    float expectedF = 0, estimatedF = 0;

    Plot(const PitchEstimator& estimator, bool harmonic, bool logx, bool logy, float scale):
        estimator(estimator),harmonic(harmonic),logx(logx),logy(logy),scale(scale){}
    float x(float f) { return (log2(f)-log2((float)iMin))/(log2((float)iMax)-log2((float)iMin)); }
    float s(uint f) { return data[f]; }
    void render(int2 position, int2 size) {
        assert_(iMin <= iMax && iMax <= data.size, iMin, iMax, data.size);
        float sMin=inf, sMax = -inf;
        for(uint i: range(iMin, iMax)) {
            if(!logy || s(i)>0) sMin = min(sMin, s(i));
            sMax = max(sMax, s(i));
        }
        for(uint i: range(iMin, iMax)) {
            float x0 = x(i) * size.x;
            float x1 = x(i+1) * size.x;
            float y = (logy ? (s(i) ? (log2(s(i)) - log2(sMin)) / (log2(sMax)-log2(sMin)) : 0) : (s(i) - sMin) / (sMax-sMin)) * (size.y-12);
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,white);
        }
        for(uint i=64; i>=1; i--) {
            for(uint c : range(min(1u,estimator.candidates.size))) {
                const PitchEstimator::Candidate& candidate = estimator.candidates[c];
                float x = this->x(round(candidate.f0*i*sqrt(1+candidate.B*sq(i)))+0.5)*size.x - 0.5;
                float v = 1./2; //3./4-(float)i/estimator.H/2;
                line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(c==0,0,c==1,v));
            }
        }
        for(uint i : range(estimator.candidates.size)) {
            const auto& candidate = estimator.candidates[i];
            String text = dec(round(candidate.B ? 1./candidate.B : 0))+" "_+dec(candidate.H);
            if(i==0) text <<' '<< dec(1./(1-estimator.candidates[0].key/estimator.candidates[1].key));
            Text label(text,16,vec4(vec3(float(1+i)/estimator.candidates.size),1.f));
            int2 labelSize = label.sizeHint();
            float x = this->x(candidate.f0*(1+candidate.B/2))*size.x;
            label.render(int2(x,position.y+i*16),labelSize);
        }
        /*const PitchEstimator::Candidate* best = 0;
        for(const PitchEstimator::Candidate& candidate : estimator.candidates)
            if(!best || abs(candidate.f0-expectedF)<abs(best->f0-expectedF)) best=&candidate;*/
        for(uint i=64; i>=1; i--) {
            //float x = this->x(expectedF*i*sqrt(1+(best?best->B:0)*sq(i)))*size.x;
            float x = this->x(round(expectedF*i)+0.5)*size.x;
            fill(position.x+x,position.y,position.x+x+1,position.y+size.y, vec4(0,1,0,0.5));
        }
    }
};

/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation {
    // Input
    //const uint lowKey=parseKey("A0"_)-12, highKey=parseKey("B2"_)-12;
    const uint lowKey=parseKey("F3"_)-12, highKey=parseKey("F5"_)-12;
    Audio audio = decodeAudio("/Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_);
    ref<int32> stereo = audio.data;
    const uint rate = audio.rate;

    // Signal
    uint t=0;
    float signalMaximum = 0;
    buffer<float> signal {N};

    // Analysis
    static constexpr uint N = 16384; // Analysis window size (A0 (27Hz~2K))
    const uint periodSize = 4096;
    PitchEstimator pitchEstimator {N};
    const float fMin  = N*440*exp2(-4)/rate; // A0
    Notch notch1 {1*50./rate, 1./12}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./12}; // Notch filter to remove 150Hz noise

    // UI
    Plot spectrum {pitchEstimator, false, false, false, (float)rate/N};
    OffsetPlot profile;
    VBox plots {{&spectrum, &profile}};
    Window window {&plots, int2(1050, 1680/2), "Test"_};

    // Results
    uint expectedKey = highKey+1;
    uint previousKey = 0;
    uint lastKey = highKey+1;
    uint success = 0, fail=0, tries = 0, total=0;
    float minHarmonic=-inf, minAllowHarmonic = -inf, maxDenyHarmonic = -inf, globalMaxDenyHarmonic = inf, maxHarmonic = -inf;
    float minRatio=-inf, minAllowRatio = -inf, maxDenyRatio = -inf, globalMaxDenyRatio = inf, maxRatio= -inf;
    Time totalTime;
    float maxB = 0;

    PitchEstimation() {
        assert_(rate==96000, audio.rate);
        assert_(audio.channels==2, audio.channels);
        assert_(((stereo.size/2+rate/2)/rate+3)/5==uint(1+(highKey+1)-lowKey));
        for(int i: range(stereo.size/2)) signalMaximum=::max(signalMaximum, float(stereo[i*2+0])+float(stereo[i*2+1]));
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
                if(lastKey != expectedKey) {
                    log(str(minHarmonic)+" "_+str(minAllowHarmonic)+" "_+str(globalMaxDenyHarmonic)+" "_+str(maxDenyHarmonic)+" "_+str(maxHarmonic));
                    log(str(minRatio)+" "_+str(minAllowRatio)+" "_+str(globalMaxDenyRatio)+" "_+str(maxDenyRatio)+" "_+str(maxRatio));
                }
                if(maxDenyHarmonic!=-inf) globalMaxDenyHarmonic = min(globalMaxDenyHarmonic, maxDenyHarmonic); maxDenyHarmonic = -inf;
                if(maxDenyRatio!=-inf) globalMaxDenyRatio = min(globalMaxDenyRatio, maxDenyRatio); maxDenyRatio = -inf;
                if(lastKey != expectedKey) { fail++; log("False negative", strKey(expectedKey)); break; }
                maxHarmonic=-inf, maxRatio=-inf;
            }

            // Prepares new period
            const int32* period = stereo + t*2;
            for(uint i: range(N-periodSize)) signal[i]=signal[i+periodSize];
            for(uint i: range(periodSize)) {
                float L = period[i*2+0], R = period[i*2+1];
                float x = (L+R) / signalMaximum;
                x = notch1(x);
                x = notch3(x);
                signal[N-periodSize+i] = x;
            }

            // Benchmark
            if(t<5*rate) continue; // First 5 seconds are silence (let input settle, use for noise profile if necessary)
            //if(t%(5*rate) > 4.5*rate) continue; // Transitions between notes may not be clean (affects false negative)
            expectedKey = highKey+1 - t/rate/5; // Recorded one key every 5 seconds from high key to low key
            /*if(expectedKey == parseKey("A#0"_)-12) {
                log("-", fail, "~", success,"["_+dec(round(100.*success/tries))+"%]"_,"/",tries, "["_+dec(round(100.*tries/total))+"%]"_, "/"_,total,
                    "~", "["_+dec(round(100.*success/total))+"%]\t"_+
                    dec(round((float)totalTime))+"s "_+str(round(stereo.size/2./rate))+"s "_+str((stereo.size/2*1000/rate)/((uint64)totalTime))+" xRT"_
                    );
                return;
            }*/

            float f0 = pitchEstimator.estimate(signal, fMin);
            uint key = f0 ? round(pitchToKey(f0*rate/N)) : 0; //FIXME: stretched reference

            // TODO: relax conditions and check false positives
            const float harmonicThreshold = 6;
            const float ratioThreshold = 1./4;
            float harmonic = pitchEstimator.harmonicPower;
            float power = pitchEstimator.totalPower;
            float ratio = harmonic / power;

            float keyF0 = keyToPitch(key)*N/rate;
            const float offsetF0 = f0 ? 12*log2(f0/keyF0) : 0;
            float keyPeakF = keyToPitch(round(pitchToKey(rate*pitchEstimator.fPeak/N)))*N/rate;
            const float offsetPeak = pitchEstimator.fPeak ?  12*log2(pitchEstimator.fPeak/keyPeakF) : 0;
            //const float fError = 12*log2((keyF+1./PitchEstimator::H)/keyF);

            log(dec((t/rate)/60,2)+":"_+ftoa(float(t%(60*rate))/rate,2,2)+"\t"_+strKey(expectedKey)+"\t"_+strKey(key)+"\t"_
                +str(round(f0*rate/N))+" Hz\t"_ +dec(round(100*offsetF0)) +"\t"_+ dec(round(100*offsetPeak)) +"\t"_
                +str(harmonic)+"\t"_+str(harmonic/power)+"\t"_
                +(expectedKey == key ? (f0 > fMin && harmonic > harmonicThreshold && ratio > ratioThreshold ? "O"_ : "~"_) : "X"_));

            if( f0 > fMin
                    && harmonic > harmonicThreshold /*Harmonic content*/
                    && ratio > ratioThreshold /*Single pitch*/ ) {
                assert_(key>=20 && key<21+keyCount, f0);

                lastKey = key;
                if(expectedKey==key) { maxB = max(maxB, pitchEstimator.inharmonicity); success++; }
                else {
                    fail++;

                    const float expectedF = keyToPitch(expectedKey)*N/rate;
                    spectrum.data = pitchEstimator.spectrum;
                    spectrum.iMin = min(f0, expectedF) * (64-1)/64;
                    spectrum.estimatedF = f0;
                    spectrum.expectedF = expectedF;
                    spectrum.iMax = min(N/2, uint(max(f0,expectedF)*64));

                    // Benchmark flaws
                    if(harmonic<85 && ratio<2.1 &&
                            ((t%(5*rate)<2*rate && ((offsetF0>-1./3 && key==expectedKey-1) || t%(5*rate)<rate))
                              || (t%(5*rate)>4.5*rate && key<=expectedKey+4)
                             || ((previousKey==expectedKey || previousKey==expectedKey-1 || (key==expectedKey+1 || key==expectedKey+2
                                                                                             || key==expectedKey+3))
                                  && expectedKey<=parseKey("D#0"_)))) {
                        array<uint> mistuned; for(string key: split("B1 A1 G1 F#1 F1 D#1 D1 C#1 C1 B0 A#0"_)) mistuned << parseKey(key);
                        if(((harmonic<24 && ratio<1.6 && (t%(5*rate) < rate)) || offsetF0>1./4) && mistuned.contains(expectedKey)) log("-"_); // Mistuned keys
                        else if(expectedKey<=parseKey("A0"_)) log("X"_);
                        else { log("Corner case"); break; }
                        lastKey=expectedKey;
                    } else { log("False positive",harmonic<54 && ratio<2.1); break; }
                }
                tries++;

                assert_(key>=21 && key<21+keyCount);
                float& keyOffset = profile.offsets[key-21];
                {const float alpha = 1./8; keyOffset = (1-alpha)*keyOffset + alpha*offsetF0;} // Smoothes offset changes (~1sec)
                float variance = sq(offsetF0 - keyOffset);
                float& keyVariance = profile.variances[key-21];
                {const float alpha = 1./8; keyVariance = (1-alpha)*keyVariance + alpha*variance;} // Smoothes deviation changes
            }
            previousKey = key;
            total++;

            if(key==expectedKey /*&& lastKey!=expectedKey*/) {
                maxHarmonic = max(maxHarmonic, harmonic); if(ratio > ratioThreshold) maxDenyHarmonic = max(maxDenyHarmonic, harmonic);
                maxRatio = max(maxRatio, ratio); if(harmonic > harmonicThreshold) maxDenyRatio = max(maxDenyRatio, ratio);
            } else {
                minHarmonic = max(minHarmonic, harmonic); if(ratio > ratioThreshold) minAllowHarmonic = max(minAllowHarmonic, harmonic);
                minRatio = max(minRatio, ratio); if(harmonic > harmonicThreshold) minAllowRatio = max(minAllowRatio, ratio);
            }

            //break;
        }
        if(fail) {
            log(str(minHarmonic)+" "_+str(minAllowHarmonic)+" "_+str(globalMaxDenyHarmonic)+" "_+str(maxDenyHarmonic)+" "_+str(maxHarmonic));
            log(str(minRatio)+" "_+str(minAllowRatio)+" "_+str(globalMaxDenyRatio)+" "_+str(maxDenyRatio)+" "_+str(maxRatio));
        }
        if(spectrum.data) {
            window.setTitle(strKey(expectedKey));
            window.render();
            window.show();
        }
    }
} app;
