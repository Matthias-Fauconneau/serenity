#include "thread.h"
#include "math.h"
#include "pitch.h"
#include <fftw3.h> //fftw3f
#include "ffmpeg.h"
#include "data.h"
#include "time.h"
#include "plot.h"
#include "layout.h"
#include "window.h"
#include "display.h"
#include "text.h"

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

// Maps frequency (Hz) with [fMin, fMax] to [0, 1] with a log scale
float log(float f, float fMin, float fMax) { return (log2(f)-log2(fMin))/(log2(fMax)-log2(fMin)); }

struct SpectrumPlot : Widget {
    uint rate = 0;
    ref<float> spectrum; //N/2
    uint key = 0;
    float f = 0;

    void render(int2 position, int2 size) {
        const uint N = spectrum.size*2;
        const int minKey = min(key, (uint)floor(pitchToKey(rate*f/N)));
        const int maxKey = max(key, (uint)ceil(pitchToKey(rate*f/N)))+3*12;
        float fMin = keyToPitch(minKey), fMax = keyToPitch(maxKey);
        const uint iMin = fMin*N/rate, iMax = fMax*N/rate;
        float sMax = 0;
        for(uint i: range(iMin, iMax)) sMax = max(sMax, spectrum[i]);
        if(!sMax) return;
        for(uint i: range(iMin, iMax)) {
            float x0 = log((float)i*rate/N, fMin, fMax) * size.x;
            float x1 = log((float)(i+1)*rate/N, fMin, fMax) * size.x;
            float y = spectrum[i] / sMax * (size.y-16);
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,white);
            if(spectrum[i-1] < spectrum[i] && spectrum[i] > spectrum[i+1] && spectrum[i] > sMax/16) {
                Text label(dec(round((float)i*rate/N)),16,white);
                int2 labelSize = label.sizeHint();
                float x = position.x+(x0+x1)/2;
                label.render(int2(x-labelSize.x/2,position.y+size.y-y-labelSize.y),labelSize);
            }
        }
    }
};

struct HarmonicPlot : Widget {
    static constexpr uint harmonics = PitchEstimator::harmonics;
    uint rate = 0;
    ref<real> harmonic;
    uint key = 0;
    float f = 0;
    void render(int2 position, int2 size) {
        if(!key) return;
        const uint N = harmonic.size*2;
        const int minKey = min(key, (uint)floor(pitchToKey(rate*f/N)))-12;
        const int maxKey = max(key, (uint)ceil(pitchToKey(rate*f/N)))+12;
        float fMin = keyToPitch(minKey), fMax = keyToPitch(maxKey);
        real sMax = 0;
        const uint iMin = fMin*N/rate, iMax = fMax*N/rate;
        for(uint i: range(iMin*harmonics, iMax*harmonics)) sMax = max(sMax, harmonic[i]);
        if(!sMax) return;
        real sMin = sMax;
        for(uint i: range(iMin*harmonics*2, iMax*harmonics/2)) if(harmonic[i]) sMin = min(sMin, harmonic[i]);
        for(uint i: range(harmonics*iMin, harmonics*iMax)) {
            float x0 = log((float)i*rate/N/harmonics, fMin, fMax) * size.x;
            float x1 = log((float)(i+1)*rate/N/harmonics, fMin, fMax) * size.x;
            //float s = (log2(harmonic[i]) - log2(sMin)) / (log2(sMax)-log2(sMin));
            float s =  (harmonic[i] - sMin) / (sMax-sMin);
            float y = s * (size.y-16);
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,white);
            if(harmonic[i-1] < harmonic[i] && harmonic[i] > harmonic[i+1] && s > 1./2) {
                Text label(dec(round((float)i*rate/N/harmonics)),16,white);
                int2 labelSize = label.sizeHint();
                float x = position.x+(x0+x1)/2;
                label.render(int2(x-labelSize.x/2,position.y+size.y-y-labelSize.y),labelSize);
            }
        }
        {float x = log(keyToPitch(key), fMin, fMax)*size.x;
            fill(position.x+x,position.y,position.x+x+1,position.y+size.y, vec4(0,1,0,1));}
        {float x = log(rate*f/N, fMin, fMax)*size.x;
            fill(position.x+x,position.y,position.x+x+1,position.y+size.y, vec4(1,0,0,1));}
    }
};

/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation {
    SpectrumPlot spectrum;
    HarmonicPlot harmonic;
    VBox plots {{&spectrum, &harmonic}};
    Window window {&plots, int2(1050, 1680/2), "Test"_};

    map<float,float> offsets;

    const uint lowKey=parseKey("A0"_)-12, highKey=parseKey("C3"_)-12; // FIXME: wait 5sec before recording
    Audio audio = decodeAudio("/Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_);
    ref<int32> stereo = audio.data;
    const uint rate = audio.rate;

    static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
    const uint periodSize = 4096;
    PitchEstimator pitchEstimator {N};

    Notch notch1 {1*50./rate, 1./12}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./12}; // Notch filter to remove 150Hz noise

    const uint fMin  = N*440*exp2(-4)/rate; // ~27 Hz ~ A-1

    uint lastKey = highKey;
    uint success = 0, fail=0, tries = 0, total=0;
    real minAllowMax = 0, minAllowRatio = 0;
    real maxDenyMax = 0, maxDenyRatio = 0;
    real globalMaxDenyMax = __builtin_inf(), globalMaxDenyRatio = __builtin_inf();
    Time totalTime;

    uint t=0;

    PitchEstimation() {
        assert_(rate==96000, audio.rate);
        assert_(audio.channels==2, audio.channels);
        assert_(((stereo.size/2+rate/2)/rate+3)/5==uint(highKey-lowKey+1));

        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(Key(' ')).connect(this, &PitchEstimation::next);

        next();
    }

    float signal[N];
    void next() {
        t+=periodSize;
        for(; t<=stereo.size/2-N; t+=periodSize) {
            const int32* period = stereo + t*2;
            for(uint i: range(N-periodSize)) signal[i]=signal[i+periodSize];
            for(uint i: range(periodSize)) {
                float x = (period[i*2+0]+period[i*2+1]) * 0x1p-32f; // 32 + 1 (stereo) - 1 (sign)
                x = notch1(x);
                x = notch3(x);
                signal[N-periodSize+i] = x;
            }

            uint expectedKey = highKey - t/rate/5; // Recorded one key every 5 seconds from high key to low key
            if(expectedKey == parseKey("A#0"_)-12) {  log("Success"); break; }

            float f = pitchEstimator.estimate(signal, fMin);
            uint key = round(pitchToKey(f*rate/N));

            const float ratioThreshold = 0.21;
            const float maxThreshold = 11;
            if(t>rate) {
                real max = pitchEstimator.harmonicMax;
                real power = pitchEstimator.harmonicPower;
                real ratio = max / power;
                if(expectedKey==key) {
                    if(ratio > ratioThreshold) maxDenyMax = ::max(maxDenyMax, max);
                    if(max > maxThreshold) maxDenyRatio = ::max(maxDenyRatio, ratio);
                } else {
                    if(ratio > ratioThreshold) minAllowMax = ::max(minAllowMax, max);
                    if(max > maxThreshold) minAllowRatio = ::max(minAllowRatio, ratio);
                }
            }

            if( pitchEstimator.harmonicMax > maxThreshold /*Harmonic content*/
                    && pitchEstimator.harmonicMax / pitchEstimator.harmonicPower > ratioThreshold /*Single pitch*/ ) {

                float expectedF = keyToPitch(key)*N/rate;
                const float fOffset = 12*log2(f/expectedF);
                const float fError = 12*log2((expectedF+1./PitchEstimator::harmonics)/expectedF);

                log(dec((t/rate)/60,2)+":"_+dec((t/rate)%60,2)+"\t"_+strKey(expectedKey)+"\t"_+strKey(key)+"\t"_
                    +str(round(f*rate/N))+" Hz\t"_ +dec(round(100*fOffset))+"  ("_+dec(round(100*fError))+")\t"_
                    /*+(expectedKey == key ? "O"_ : "X"_)*/);
                lastKey = key;
                if(expectedKey==key) success++;
                else {
                    spectrum.rate = rate;
                    spectrum.spectrum = pitchEstimator.spectrum;
                    spectrum.key = expectedKey;
                    spectrum.f = f;

                    harmonic.rate = rate;
                    harmonic.key = expectedKey;
                    harmonic.harmonic = pitchEstimator.harmonic;
                    harmonic.f = f;
                    log(str(minAllowMax)+" "_+str(globalMaxDenyMax)+" "_+str(minAllowRatio)+" "_+str(globalMaxDenyRatio));
                    log("False positive");
                    break;
                }
                tries++;

                //offsets.insertMulti(key-42, 100*(fError<kError ? fOffset : kOffset));
            }
            total++;

            uint nextKey = highKey - (t+periodSize)/rate/5;
            if(nextKey != expectedKey) {
                globalMaxDenyMax = min(globalMaxDenyMax, maxDenyMax);
                globalMaxDenyRatio = min(globalMaxDenyRatio, maxDenyRatio);
                maxDenyMax = 0, maxDenyRatio = 0;
                if(lastKey != expectedKey) {
                    log(str(minAllowMax)+" "_+str(globalMaxDenyMax)+" "_+str(minAllowRatio)+" "_+str(globalMaxDenyRatio));
                    log("False negative", strKey(expectedKey));
                    break;
                }
            }
        }
        log("-", fail, "~", success,"["_+dec(round(100.*success/tries))+"%]"_,"/",tries, "["_+dec(round(100.*tries/total))+"%]"_, "/"_,total,
            "~", "["_+dec(round(100.*success/total))+"%]\t"_+
            dec(round((float)totalTime))+"s "_+str(round(stereo.size/2./rate))+"s "_+str((stereo.size/2*1000/rate)/((uint64)totalTime))+" xRT"_
            );
        /*if(spectrum.spectrum || harmonic.harmonic) {
            window.setTitle(strKey(harmonic.key));
            window.show();
            window.render();
        }*/
    }
} app;
