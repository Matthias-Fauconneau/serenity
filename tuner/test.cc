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
    ref<float> spectrum; // also harmonic product spectrum
    uint iMin = 0, iMax = 0;
    float logx(float f) { return (log2(f)-log2((float)iMin))/(log2((float)iMax)-log2((float)iMin)); }
    const bool logy = true;
    void render(int2 position, int2 size) {
        assert_(iMin <= iMax && iMax <= spectrum.size, iMin, iMax, spectrum.size);
        float sMin=__builtin_inf(), sMax = -__builtin_inf();
        for(uint i: range(iMin, iMax)) {
            if(!logy || spectrum[i]>0) sMin = min(sMin, spectrum[i]);
            sMax = max(sMax, spectrum[i]);
        }
        log("min", sMin, "max", sMax);
        for(uint i: range(iMin, iMax)) {
            float x0 = logx(i) * size.x;
            float x1 = logx(i+1) * size.x;
            float s = logy ? (log2(spectrum[i]) - log2(sMin)) / (log2(sMax)-log2(sMin)) : (spectrum[i] - sMin) / (sMax-sMin);
            float y = s * (size.y-16);
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,white);
        }
        {float x = logx((iMin+iMax)/2.)*size.x;
            fill(position.x+x,position.y,position.x+x+1,position.y+size.y, vec4(0,1,0,1));}
    }
};

/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation {
    Plot spectrum;
    //Plot harmonic;
    OffsetPlot profile;
    VBox plots {{&spectrum/*, &harmonic*//*, &profile*/}};
    Window window {&plots, int2(1050, 1680/2), "Test"_};

    map<float,float> offsets;

    const uint lowKey=parseKey("F3"_)-12, highKey=parseKey("F5"_)-12; // FIXME: wait 5sec before recording
    Audio audio = decodeAudio("/Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_);
    ref<int32> stereo = audio.data;
    const uint rate = audio.rate;

    static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
    const uint periodSize = 4096;
    PitchEstimator pitchEstimator {N};

    Notch notch1 {1*50./rate, 1./12}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./12}; // Notch filter to remove 150Hz noise

    const float fMin  = N*440*exp2(-4)/rate; // ~27 Hz ~ A-1

    uint expectedKey = highKey+1;
    uint lastKey = highKey+1;
    uint success = 0, fail=0, tries = 0, total=0;
    float minAllowMax = -__builtin_inf(), maxDenyMax = -__builtin_inf(), globalMaxDenyMax = __builtin_inf();
    float minAllowRatio = -__builtin_inf(), maxDenyRatio = -__builtin_inf(), globalMaxDenyRatio = __builtin_inf();
    Time totalTime;

    uint t=0;
    int32 signalMaximum = 1;
    buffer<float> signal {N};

    PitchEstimation() {
        assert_(rate==96000, audio.rate);
        assert_(audio.channels==2, audio.channels);
        assert_(((stereo.size/2+rate/2)/rate+3)/5==uint(1+(highKey+1)-lowKey));

        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();});
        //window.localShortcut(Key(' ')).connect(this, &PitchEstimation::next);
        window.frameReady.connect(this, &PitchEstimation::next);

        profile.reset();
        next();
    }

    void next() {
        if(fail) return;
        t+=periodSize;
        for(; t<=stereo.size/2-N; t+=periodSize) {
            const int32* period = stereo + t*2;
            for(uint i: range(N-periodSize)) signal[i]=signal[i+periodSize];
            for(uint i: range(periodSize)) {
                signalMaximum = max(abs(period[i*2+0]), abs(period[i*2+1]));
                float x = (float) (period[i*2+0]+period[i*2+1]) / (2.f*signalMaximum); // FIXME
                x = notch1(x);
                x = notch3(x);
                signal[N-periodSize-1+i] = x;
            }
            if(t<5*rate) continue; // First 5 seconds are silence (let input settle, use for noise profile if necessary)

            expectedKey = highKey+1 - t/rate/5; // Recorded one key every 5 seconds from high key to low key
            if(expectedKey == parseKey("A#0"_)-12) {
                log("-", fail, "~", success,"["_+dec(round(100.*success/tries))+"%]"_,"/",tries, "["_+dec(round(100.*tries/total))+"%]"_, "/"_,total,
                    "~", "["_+dec(round(100.*success/total))+"%]\t"_+
                    dec(round((float)totalTime))+"s "_+str(round(stereo.size/2./rate))+"s "_+str((stereo.size/2*1000/rate)/((uint64)totalTime))+" xRT"_
                    );
                return;
            }

            float f = pitchEstimator.estimate(signal, fMin);
            uint key = round(pitchToKey(f*rate/N));
            assert_(key>=20 && key<21+keyCount, key, f, fMin, pitchToKey(fMin*rate/N));

            /*harmonic.spectrum = pitchEstimator.harmonic;
            harmonic.iMin  = keyToPitch(key-0.5)*N*pitchEstimator.harmonics/rate;
            harmonic.iMax = keyToPitch(key+0.5)*N*pitchEstimator.harmonics/rate;*/

            const float maxThreshold = 1.2;
            const float ratioThreshold = 7.46;
            float max = pitchEstimator.harmonicMax;
            float power = pitchEstimator.harmonicPower;
            float ratio = max - power;
            if(expectedKey==key) {
                if(ratio > ratioThreshold) maxDenyMax = ::max(maxDenyMax, max);
                if(max > maxThreshold) maxDenyRatio = ::max(maxDenyRatio, ratio);
            } else {
                if(ratio > ratioThreshold) minAllowMax = ::max(minAllowMax, max);
                if(max > maxThreshold) minAllowRatio = ::max(minAllowRatio, ratio);
            }

            float expectedF = keyToPitch(key)*N/rate;
            const float fOffset = 12*log2(f/expectedF);
            const float fError = 12*log2((expectedF+1./PitchEstimator::harmonics)/expectedF);

            log(dec((t/rate)/60,2)+":"_+dec((t/rate)%60,2)+"\t"_+strKey(expectedKey)+"\t"_+strKey(key)+"\t"_
                +str(round(f*rate/N))+" Hz\t"_ +dec(round(100*fOffset))+"  ("_+dec(round(100*fError))+")\t"_
                +str(max)+"\t"_+str(power)+"\t"_+/*str(max/power)+"\t"_+*/str(max-power)+"\t"_
                +(expectedKey == key ? "O"_ : "X"_));

            if(max > maxThreshold /*Harmonic content*/
                    && ratio > ratioThreshold /*Single pitch*/ ) {

                lastKey = key;
                if(expectedKey==key) success++; else { log("False positive"); fail++; break; }
                tries++;

                assert_(key>=21 && key<21+keyCount);
                float& keyOffset = profile.offsets[key-21];
                {const float alpha = 1./8; keyOffset = (1-alpha)*keyOffset + alpha*fOffset;} // Smoothes offset changes (~1sec)
                float variance = sq(fOffset - keyOffset);
                float& keyVariance = profile.variances[key-21];
                {const float alpha = 1./8; keyVariance = (1-alpha)*keyVariance + alpha*variance;} // Smoothes deviation changes
            }
            total++;

            uint nextKey = highKey+1 - (t+periodSize)/rate/5;
            if(nextKey != expectedKey) {
                globalMaxDenyMax = min(globalMaxDenyMax, maxDenyMax); maxDenyMax = 0;
                globalMaxDenyRatio = min(globalMaxDenyRatio, maxDenyRatio); maxDenyRatio = 0;
                if(lastKey != expectedKey) { fail++; log("False negative", strKey(expectedKey)); break; }
            }

            //break;
        }
        if(fail) {
            log(str(minAllowMax)+" "_+str(globalMaxDenyMax)+" "_+str(minAllowRatio)+" "_+str(globalMaxDenyRatio));
            log(log2((float)signalMaximum));
        }
        /*if(harmonic.harmonic) {
            window.setTitle(strKey(expectedKey));
            window.render();
            window.show();
        }*/
    }
} app;
