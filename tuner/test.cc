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
        assert_(0 < iMin && iMin <= iMax && iMax <= data.size);
        float sMin=inf, sMax = -inf;
        for(uint i: range(iMin, iMax)) {
            if(!logy || s(i)>0) sMin = min(sMin, s(i));
            sMax = max(sMax, s(i));
        }
        float mean = estimator.meanDensity;
        for(int i: range(log2(estimator.peakThreshold)+1)) { // from full scale
            float s = sMax*exp2(-i); assert_(s);
            float y = (logy ? (log2(s) - log2(mean)) / (log2(sMax)-log2(mean)) : (s / sMax)) * (size.y-12);
            line(position.x,position.y+size.y-y-0.5,position.x+size.x,position.y+size.y-y+0.5,vec4(0,1,0,0.5));
        }
        for(int i: range(log2(estimator.meanThreshold)+1)) { // from background
            float s = mean*exp2(i); assert_(s);
            float y = (logy ? (log2(s) - log2(mean)) / (log2(sMax)-log2(mean)) : (s / sMax)) * (size.y-12);
            line(position.x,position.y+size.y-y-0.5,position.x+size.x,position.y+size.y-y+0.5,vec4(1,0,0,0.5));
        }
        float limit = estimator.limit;
        { // Peak count limit
            float s = limit;
            float y = (logy ? (log2(s) - log2(mean)) / (log2(sMax)-log2(mean)) : (s / sMax)) * (size.y-12);
            line(position.x,position.y+size.y-y-0.5,position.x+size.x,position.y+size.y-y+0.5,vec4(1,0,0,1));
        }
        for(uint i: range(iMin, iMax)) {
            float x0 = x(i) * size.x;
            float x1 = x(i+1) * size.x;
            float y = (logy ? (s(i) ? (log2(s(i)) - log2(mean)) / (log2(sMax)-log2(mean)) : 0) : (s(i) / sMax)) * (size.y-12);
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,vec4(1,1,1,0.5));
        }
        for(uint i=21; i>=1; i--) {
            for(uint c : range(min(3u,estimator.candidates.size))) {
                const PitchEstimator::Candidate& candidate = estimator.candidates[c];
                uint f = round(candidate.f0*i*sqrt(1+candidate.B*sq(i)));
                const int radius = estimator.peakRadius;
                if(/*f<radius ||*/ f>=data.size/*-radius*/) continue;
                float s=0; for(int df: range(max(0,int(f-radius)),min<int>(data.size,f+radius+1))) s=max(s, data[df]);
                vec4 color = vec4(vec3(float(1+c)/estimator.candidates.size),1.f);
                if(i!=1 && s < limit) {
                    const int extended = 8;
                    for(int df: range(max(0,int(f-radius-extended)),min<int>(data.size,f+radius+extended+1))) s=max(s, data[df]);
                    color.y=0, color.z=0;
                    if(i!=1 && s < limit) continue;
                }
                float v = i==1 ? 1 : min(1., 2 * (log2(s) - log2(mean)) / (log2(sMax)-log2(mean)));
                v = max(1.f/2, v);
                float x = this->x(f+0.5)*size.x - 0.5;
                line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(c!=1,c!=0,0,v));
                if(s < limit) continue;
                Text label(dec(i),16,color);
                int2 labelSize = label.sizeHint();
                label.render(int2(x,position.y+(c+i%16)*16+i/16),labelSize);
            }
        }
        for(uint i : range(estimator.candidates.size)) {
            const auto& candidate = estimator.candidates[i];
            if(!candidate.f0) continue;
            String text = strKey(pitchToKey(96000*candidate.f0/estimator.N))+" "_+dec(round(candidate.B ? 1./candidate.B : 0))+" "_+
                    /*dec(candidate.df)+" "_+dec(candidate.H)+" "_+*/dec(candidate.peakCount);
            //if(i==0) text <<' '<< dec(1./(1-estimator.candidates[0].key/estimator.candidates[1].key));
            //if(i==0) text <<' '<< dec(estimator.candidates[1].key/estimator.candidates[0].key);
            Text label(text,16,vec4(vec3(float(1+i)/estimator.candidates.size),1.f));
            int2 labelSize = label.sizeHint();
            float x = this->x(candidate.f0*(1+candidate.B/2))*size.x;
            label.render(int2(x,position.y+i*16),labelSize);
        }
        /*const PitchEstimator::Candidate* best = 0;
        for(const PitchEstimator::Candidate& candidate : estimator.candidates)
            if(!best || abs(candidate.f0-expectedF)<abs(best->f0-expectedF)) best=&candidate;*/
        for(uint i=64; i>=1; i--) {
            float f = expectedF*i;
            float x = this->x(round(f)+0.5)*size.x;
            float v = i==1 ? 1 : min(1., 2 * (log2(s(f)) - log2(mean)) / (log2(sMax)-log2(mean)));
            v = max(1.f/2, v);
            line(position.x+x,position.y,position.x+x+1,position.y+size.y, vec4(0,0,1,v));
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
    const float fMax = N*440*exp2(3)/rate; // A7
    Notch notch1 {1*50./rate, 1./12}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./12}; // Notch filter to remove 150Hz noise

    // UI
    Plot spectrum {pitchEstimator, false, false, true, (float)rate/N};
    OffsetPlot profile;
    VBox plots {{&spectrum/*, &profile*/}};
    Window window {&plots, int2(1050, 1680/2), "Test"_};

    // Results
    int expectedKey = highKey+1;
    int previousKey = 0;
    int lastKey = highKey+1;
    uint success = 0, fail=0, tries = 0, total=0;
    float minHarmonic=-inf, minAllowHarmonic = -inf, maxDenyHarmonic = -inf, globalMaxDenyHarmonic = inf, maxHarmonic = -inf;
    float minRatio=-inf, minAllowRatio = -inf, maxDenyRatio = -inf, globalMaxDenyRatio = inf, maxRatio= -inf;
    Time totalTime;
    float maxB = 0;
    int dfMax=0, dfMin=0;

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

            float f0 = pitchEstimator.estimate(signal, round(fMin), round(fMax));
            assert_(f0==0 || pitchToKey(f0*rate/N)>-7, fMin, pitchToKey(f0*rate/N), 1./pitchEstimator.inharmonicity);
            int key = f0 ? round(pitchToKey(f0*rate/N)) : 0; //FIXME: stretched reference

            // TODO: relax conditions and check false positives
            const float harmonicThreshold = 4; //4
            const float ratioThreshold = 1./5; //1./5
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
                +str(harmonic)+"\t"_+str(harmonic/power)+"\t"_+str(pitchEstimator.peakCount)+"\t"_
                +str(pitchEstimator.inharmonicity?1./pitchEstimator.inharmonicity:0)+"\t"_
                +(expectedKey == key ? (f0 > fMin && harmonic > harmonicThreshold && ratio > ratioThreshold ? "O"_ : "~"_) : "X"_));

            if( f0 > fMin
                    && pitchEstimator.peakCount > 1
                    && harmonic > harmonicThreshold /*Harmonic content*/
                    && ratio > ratioThreshold /*Single pitch*/ ) {
                assert_(key>=20 && key<21+keyCount, f0);

                if(expectedKey==key) {
                    if(pitchEstimator.inharmonicity>maxB) log(1./(maxB=pitchEstimator.inharmonicity));
                    if(pitchEstimator.bestDf>dfMax) log(dfMax=pitchEstimator.bestDf);
                    if(pitchEstimator.bestDf<dfMin) log(dfMin=pitchEstimator.bestDf);
                    success++;
                    lastKey = key;
                }
                else {
                    fail++;

                    const float expectedF = keyToPitch(expectedKey)*N/rate;
                    spectrum.data = pitchEstimator.spectrum;
                    spectrum.iMin = min(f0, expectedF);
                    spectrum.estimatedF = f0;
                    spectrum.expectedF = expectedF;
                    spectrum.iMax = min(N/2, uint(max(f0,expectedF)*32));

                    /*for(uint c : range(min(2u,pitchEstimator.candidates.size))) {
                        for(uint n=64; n>=1; n--) {
                            const PitchEstimator::Candidate& candidate = pitchEstimator.candidates[c];
                            int f = round(candidate.f0*n*sqrt(1+candidate.B*sq(n)));
                            const int radius = pitchEstimator.peakRadius;
                            //(f<radius || f >= int(pitchEstimator.spectrum.size-radius)) continue;
                            assert_(f>=radius && f < int(pitchEstimator.spectrum.size-radius));
                            for(int df: range(-radius, radius+1)) {
                                if(pitchEstimator.spectrum[f+df] > pitchEstimator.maxPeak/pitchEstimator.peakThreshold) {
                                    log(candidate.peakCount, n, f, df, f+df, pitchEstimator.spectrum[f+df]);
                                    break;
                                }
                            }
                        }
                    }*/

                    // Relax for hard cases
                    if(ratio<1./2 &&
                            (
                                (t%(5*rate)<3*rate && offsetF0>1./3 && key==expectedKey-1)
                                || (t%(5*rate)<2*rate && ((offsetF0>-1./3 && key==expectedKey-1) || t%(5*rate)<rate))
                              || (t%(5*rate)>4.5*rate && key<=expectedKey+4)
                             || ((previousKey==expectedKey || previousKey==expectedKey-1 ||
                                  (key==expectedKey+1 || key==expectedKey+2 || key==expectedKey+3)) && expectedKey<=parseKey("D#0"_))
                             || (t%(5*rate)<2*rate && previousKey==expectedKey && ratio<1./3 /*&& key==expectedKey-12*/))) {
                        if(0) {}
                        else if(offsetF0>1./3 && key==expectedKey-1 && apply(split("A2 D1 C#1 C1 A#0"_), parseKey).contains(expectedKey)) {
                            log("-"_); lastKey=expectedKey; // Avoid false negative from mistune
                        }
                        else if(offsetF0>1./4 && key==expectedKey-1 && apply(split("D4 C#1"_), parseKey).contains(expectedKey)) log("-"_);
                        else if(t%(5*rate) < 2*rate && ratio<1./3 && apply(split("C4 A3"_), parseKey).contains(expectedKey)) log("/"_);
                        else if((ratio<1./3 && (t%(5*rate) < rate)) || (t%(5*rate) < rate/2)) log("!"_); // Attack
                        else if(t%(5*rate)>4.5*rate && key<=expectedKey+4) log("."_); // Release
                        else if(expectedKey<=parseKey("A0"_)) log("_"_); // Bass strings
                        else { log("Corner case"); break; }
                    } else { log("False positive",ratio<1./2, t%(5*rate)<2*rate, previousKey==expectedKey,  lastKey==expectedKey,  ratio<1./3, key==expectedKey-12); break; }
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
            log(str(minHarmonic)+" "_+str(minAllowHarmonic)+" "_+str(globalMaxDenyHarmonic)+" "_+str(maxDenyHarmonic)+" "_+str(maxHarmonic));
            log(str(minRatio*0x100)+" "_+str(minAllowRatio*0x100)+" "_+str(globalMaxDenyRatio*0x100)+" "_+str(maxDenyRatio*0x100)+" "_+str(maxRatio*0x100));
        }
        log(maxB?1./maxB:0, dfMin, dfMax);
        if(spectrum.data) {
            window.setTitle(strKey(expectedKey));
            window.render();
            window.show();
        }
    }
} app;
