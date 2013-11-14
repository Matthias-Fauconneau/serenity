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
    const bool logx;
    const bool logy;
    const float scale;
    uint iMin = 0, iMax = 0;
    ref<float> data;
    float expectedF = 0, estimatedF = 0;

    Plot(const PitchEstimator& estimator, bool logx, bool logy, float scale):estimator(estimator),logx(logx),logy(logy),scale(scale){}
    float x(float f) { return (log2(f)-log2((float)iMin))/(log2((float)iMax)-log2((float)iMin)); }
    void render(int2 position, int2 size) {
        assert_(iMin <= iMax && iMax <= data.size, iMin, iMax, data.size);
        float sMin=__builtin_inf(), sMax = -__builtin_inf();
        for(uint i: range(iMin, iMax)) {
            if(!logy || data[i]>0) sMin = min(sMin, data[i]);
            sMax = max(sMax, data[i]);
        }
        for(uint i: range(iMin, iMax)) {
            float x0 = x(i) * size.x;
            float x1 = x(i+1) * size.x;
            float s = logy ? (data[i] ? (log2(data[i]) - log2(sMin)) / (log2(sMax)-log2(sMin)) : 0) : (data[i] - sMin) / (sMax-sMin);
            float y = s * (size.y-16);
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,white);
            if(scale && data[i-1] < data[i] && data[i] > data[i+1] && data[i]-sMin > (sMax-sMin)/2) {
                Text label(dec(round(scale*i)),16,white);
                int2 labelSize = label.sizeHint();
                float x = position.x+(x0+x1)/2;
                label.render(int2(x-labelSize.x/2,position.y+size.y-y-labelSize.y),labelSize);
            }
        }
        for(uint i=estimator.harmonics; i>=1; i--) {
            {float x = this->x(expectedF*i*sqrt(1+estimator.B(expectedF)*sq(i)))*size.x;
                fill(position.x+x,position.y,position.x+x+1,position.y+size.y, vec4(0,1,0,0.5));}
            {float x = this->x(estimatedF*i*sqrt(1+estimator.B(estimatedF)*sq(i)))*size.x;
                fill(position.x+x,position.y,position.x+x+1,position.y+size.y, vec4(1,0,0,0.5));}
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
    Plot spectrum {pitchEstimator, false, true, (float)rate/N};
    Plot harmonic {pitchEstimator, false, true, (float)rate/N/PitchEstimator::harmonics};
    OffsetPlot profile;
    VBox plots {{&spectrum, &harmonic, &profile}};
    Window window {&plots, int2(1050, 1680/2), "Test"_};

    // Results
    uint expectedKey = highKey+1;
    uint lastKey = highKey+1;
    uint success = 0, fail=0, tries = 0, total=0;
    real minAllowMax = -__builtin_inf(), maxDenyMax = -__builtin_inf(), globalMaxDenyMax = __builtin_inf();
    real minAllowRatio = -__builtin_inf(), maxDenyRatio = -__builtin_inf(), globalMaxDenyRatio = __builtin_inf();
    real maxMax = -__builtin_inf(), maxRatio= -__builtin_inf();
    Time totalTime;

    PitchEstimation() {
        assert_(rate==96000, audio.rate);
        assert_(audio.channels==2, audio.channels);
        assert_(((stereo.size/2+rate/2)/rate+3)/5==uint(1+(highKey+1)-lowKey));
        for(int i: range(stereo.size/2)) signalMaximum=::max(signalMaximum, float(stereo[i*2+0])+float(stereo[i*2+1]));
        log(log2(signalMaximum));

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
                globalMaxDenyMax = min(globalMaxDenyMax, maxDenyMax); maxDenyMax = -__builtin_inf();
                globalMaxDenyRatio = min(globalMaxDenyRatio, maxDenyRatio); maxDenyRatio = -__builtin_inf();
                if(lastKey != expectedKey) { fail++; log("False negative", strKey(expectedKey)); break; }
                maxMax=-__builtin_inf(), maxRatio=-__builtin_inf();
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
            if(t%(5*rate) > 4.5*rate) continue; // Transitions between notes may not be clean (affects false negative)
            expectedKey = highKey+1 - t/rate/5; // Recorded one key every 5 seconds from high key to low key
            /*if(expectedKey == parseKey("A#0"_)-12) {
                log("-", fail, "~", success,"["_+dec(round(100.*success/tries))+"%]"_,"/",tries, "["_+dec(round(100.*tries/total))+"%]"_, "/"_,total,
                    "~", "["_+dec(round(100.*success/total))+"%]\t"_+
                    dec(round((float)totalTime))+"s "_+str(round(stereo.size/2./rate))+"s "_+str((stereo.size/2*1000/rate)/((uint64)totalTime))+" xRT"_
                    );
                return;
            }*/

            float f = pitchEstimator.estimate(signal, fMin, fMax);
            uint key = round(pitchToKey(f*rate/N));
            assert_(key>=20 && key<21+keyCount, f);

            const real maxThreshold = -6;
            const real ratioThreshold = 3;
            real max = pitchEstimator.harmonicMax;
            real power = pitchEstimator.harmonicPower;
            real ratio = max - power;

            float keyF = keyToPitch(key)*N/rate;
            const float fOffset = 12*log2(f/keyF);
            const float fError = 12*log2((keyF+1./PitchEstimator::harmonics)/keyF);

            log(dec((t/rate)/60,2)+":"_+dec((t/rate)%60,2)+"\t"_+strKey(expectedKey)+"\t"_+strKey(key)+"\t"_
                +str(round(f*rate/N))+" Hz\t"_ +dec(round(100*fOffset))+"  ("_+dec(round(100*fError))+")\t"_
                +str(max)+"\t"_+str(max-power)+"\t"_
                +(expectedKey == key ? "O"_ : "X"_));

            if( max > maxThreshold /*Harmonic content*/
                    && ratio > ratioThreshold /*Single pitch*/ ) {

                lastKey = key;
                if(expectedKey==key) success++;
                else {
                    fail++;

                    const float expectedF = keyToPitch(expectedKey)*N/rate;
                    spectrum.data = pitchEstimator.spectrum;
                    spectrum.iMin = min(f, expectedF);
                    spectrum.estimatedF = f;
                    spectrum.expectedF = expectedF;
                    spectrum.iMax = ::max(f,expectedF)*(pitchEstimator.harmonics+1);

                    harmonic.data = pitchEstimator.harmonic;
                    harmonic.iMin  = keyToPitch(min(key,expectedKey)-0.5)*N*pitchEstimator.harmonics/rate;
                    harmonic.estimatedF = f*pitchEstimator.harmonics;
                    harmonic.expectedF = expectedF*pitchEstimator.harmonics;
                    harmonic.iMax = keyToPitch(::max(key,expectedKey)+0.5)*N*pitchEstimator.harmonics/rate;

                    log("False positive");
                    break;
                }
                tries++;

                assert_(key>=21 && key<21+keyCount);
                float& keyOffset = profile.offsets[key-21];
                {const float alpha = 1./8; keyOffset = (1-alpha)*keyOffset + alpha*fOffset;} // Smoothes offset changes (~1sec)
                float variance = sq(fOffset - keyOffset);
                float& keyVariance = profile.variances[key-21];
                {const float alpha = 1./8; keyVariance = (1-alpha)*keyVariance + alpha*variance;} // Smoothes deviation changes
            }
            total++;

            if(expectedKey==key) {
                maxMax = ::max(maxMax, max); if(ratio > ratioThreshold) maxDenyMax = ::max(maxDenyMax, max);
                maxRatio = ::max(maxRatio, ratio); if(max > maxThreshold) maxDenyRatio = ::max(maxDenyRatio, ratio);
            } else {
                if(ratio > ratioThreshold) minAllowMax = ::max(minAllowMax, max);
                if(max > maxThreshold) minAllowRatio = ::max(minAllowRatio, ratio);
            }

            //break;
        }
        if(fail) {
            log(str(minAllowMax)+" "_+str(globalMaxDenyMax)+" "_+str(maxDenyMax)+" "_+str(maxMax));
            log(str(minAllowRatio)+" "_+str(globalMaxDenyRatio)+" "_+str(maxDenyRatio)+" "_+str(maxRatio));
            assert_(log2((float)signalMaximum)<32);
        }
        if(harmonic.data) {
            window.setTitle(strKey(expectedKey));
            window.render();
            window.show();
        }
    }
} app;
