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

// Maps frequency (Hz) with [fMin, fMax] to [0, 1] with a log scale
float log(float f, float fMin, float fMax) { return (log2(f)-log2(fMin))/(log2(fMax)-log2(fMin)); }

struct FrequencyPlot : Widget {
    uint key = 0;
    buffer<float> data; //N/2
    uint rate;
    float f = 0;

    void render(int2 position, int2 size) {
        float y0 = position.y+size.y;
        const uint N = data.size*2;
        int minKey = key-12, maxKey = key+4*12;
        minKey = min(minKey, (int)floor(pitchToKey(rate*f/N)));
        maxKey = max(maxKey, (int)ceil(pitchToKey(rate*f/N)));
        float fMin = keyToPitch(minKey), fMax = keyToPitch(maxKey);
        const uint iMin = fMin*N/rate, iMax = fMax*N/rate;
        assert(iMax < N/2);
        float sMax = 0;
        for(uint i: range(iMin, iMax)) sMax = max(sMax, data[i]);
        if(!sMax) return;
        for(uint i: range(iMin, iMax)) {
            float x0 = log((float)i*rate/N, fMin, fMax) * size.x;
            float x1 = log((float)(i+1)*rate/N, fMin, fMax) * size.x;
            float y = data[i] / sMax * (size.y-16);
            fill(position.x+x0,y0-y,position.x+x1,y0,white);
            if(data[i-1] < data[i] && data[i] > data[i+1] && data[i] > sMax/16) {
                Text label(dec(round((float)i*rate/N)),16,white);
                int2 size = label.sizeHint();
                float x = position.x+(x0+x1)/2;
                label.render(int2(x-size.x/2,y0-y-size.y),size);
            }
        }
    }
};

struct PeriodPlot : Widget {
    uint key = 0;
    buffer<float> signal;
    buffer<float> data;
    buffer<float> spectrum;
    uint rate;
    uint f = 0;
    //uint fPeak = 0;
    uint N = 0;
    void render(int2 position, int2 size) {
        if(!key) return;
        float y0 = position.y+size.y;
        int minKey = key-12, maxKey = key+4*12;
        minKey = min(minKey, (int)floor(pitchToKey(rate*f/N)));
        maxKey = max(maxKey, (int)ceil(pitchToKey(rate*f/N)));
        float fMin = keyToPitch(minKey), fMax = keyToPitch(maxKey);
        int kMin = rate/fMax, kMax = rate/fMin;
        kMin = max(kMin, 1);
        kMax = min<uint>(kMax, data.size);
        for(uint k: range(kMin,kMax)) { // Scans forward (decreasing frequency) until local maximum
            if(data[k]) continue;
            float sum = autocorrelation(signal, k, N);
            data[k] = sum;
        }
        float sMax = 0;
        //for(uint k: range(kMin, kMax)) sMax = max(sMax, data[k]);
        const uint iMin = fMin*N/rate, iMax = fMax*N/rate;
        /*for(uint i: range(iMin*12, iMax*12)) {
            float product=1; for(uint n : range(2,12)) product *= spectrum[n*i/12];
            if(!data[i]) data[i] = product;
        }*/
        for(uint i: range(iMin*12, iMax*12)) sMax = max(sMax, data[i]);
        if(!sMax) return;
        float sMin = sMax;
        for(uint i: range(iMin*12, iMax*12)) if(data[i]) sMin = min(sMin, data[i]);
        /*for(uint k: range(kMin, kMax)) {
            float f0 = (float)rate/(k+1), f1 = (float)rate/k;
            float x0 = log(f0, fMin, fMax)*size.x, x1 = log(f1, fMin, fMax)*size.x;
            float y = data[k] / sMax * size.y;
            fill(position.x+x0,y0-y,position.x+x1,y0,white);
        }*/
        for(uint i: range(12*iMin, 12*iMax)) {
            float x0 = log((float)i*rate/N/12, fMin, fMax) * size.x;
            float x1 = log((float)(i+1)*rate/N/12, fMin, fMax) * size.x;
            //float y = data[i] / sMax * (size.y-16);
            float y = (log2(data[i]) - log2(sMin)) / (log2(sMax)-log2(sMin)) * (size.y-16);
            fill(position.x+x0,y0-y,position.x+x1,y0,white);
        }
        /*for(uint i: range(23)) {
            uint k0 = round(i*N/(fPeak+0.5));
            float f0 = (float)rate/(k0+1), f1 = (float)rate/k0;
            float x0 = log(f0, fMin, fMax)*size.x, x1 = log(f1, fMin, fMax)*size.x;
            float y1 = position.y;
            fill(position.x+x0,y1,position.x+max(x0+1,x1),y0,blue);
        }*/
        for(int i: range(0,2)) {
            uint k = rate/keyToPitch(key) * exp2(-i);
            float f0 = (float)rate/(k+1), f1 = (float)rate/k;
            float x0 = log(f0, fMin, fMax)*size.x, x1 = log(f1, fMin, fMax)*size.x;
            float y1 = position.y;
            fill(position.x+x0,y1,position.x+max(x0+1,x1),y0, vec4(0,exp2(-abs(i)),0,1));
        }
        for(int i: range(0,2)) {
            float f = this->f * exp2(-i);
            float f0 = rate*(f-1./24)/N, f1 = rate*(f+1./24)/N;
            float x0 = log(f0, fMin, fMax)*size.x, x1 = log(f1, fMin, fMax)*size.x;
            float y1 = position.y;
            fill(position.x+x0,y1,position.x+max(x0+1,x1),y0, vec4(exp2(-abs(i)),0,0,1));
        }
    }
};

struct HarmonicProductPlot : Widget {
    uint key;
    buffer<float> data; //N/2
    uint rate;
    uint k = 0;
    uint N;

    uint first=2, last=3;
    uint smooth = -1;
    float hps(uint k) {
        float product=1;
        for(uint n : range(first, last+1)) {
            uint i = n*N/k;
            float sum = n<smooth ? data[i] : data[i-1]+data[i]+data[i+1];
            product *= sum;
        }
        return product;
    }
    void render(int2 position, int2 size) {
        float y0 = position.y+size.y;
        int minKey = key-12, maxKey = key+4*12;
        minKey = min(minKey, (int)floor(pitchToKey(rate/k)));
        maxKey = max(maxKey, (int)ceil(pitchToKey(rate/k)));
        float fMin = keyToPitch(minKey), fMax = keyToPitch(maxKey);
        int kMin = rate/fMax, kMax = rate/fMin;
        float sMax = 0;
        for(uint k: range(kMin, kMax)) sMax = max(sMax, hps(k));
        float sMin = sMax;
        for(uint k: range(kMin, kMax)) sMin = min(sMin, hps(k));
        if(!sMax) return;
        for(uint k: range(kMin, kMax)) {
            float f0 = (float)rate/(k+1), f1 = (float)rate/k;
            float x0 = log(f0, fMin, fMax) * size.x;
            float x1 = log(f1, fMin, fMax) * size.x;
            float y = (log2(hps(k)) - log2(sMin)) / (log2(sMax)-log2(sMin)) * (size.y-16);
            //float y = hps(k) / sMax * (size.y-16);
            fill(position.x+x0,y0-y,position.x+x1,y0,white);
            if(hps(k+1) < hps(k) && hps(k) > hps(k-1) && hps(k) > sMax/16) {
                Text label(dec(round((float)rate/k)),16,white);
                int2 size = label.sizeHint();
                float x = position.x+(x0+x1)/2;
                label.render(int2(x-size.x/2,y0-y-size.y),size);
            }
        }
    }
};

/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation {
    //VList<Plot> plots;
    FrequencyPlot frequency;
    PeriodPlot period;
    //HarmonicProductPlot harmonic[5];
    VBox plots {{&frequency, &period/*, &harmonic[0], &harmonic[1], &harmonic[2], &harmonic[3], &harmonic[4]*/}};
    Window window {&plots, int2(1050, 1680/2), "Test"_};

    map<float,float> offsets;

    const uint lowKey=parseKey("A0"_)-12, highKey=parseKey("C3"_)-12; // FIXME: wait 5sec before recording
    Audio audio = decodeAudio("/Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_);
    ref<int32> stereo = audio.data;
    const uint rate = audio.rate;


    static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
    const uint periodSize = 4096;
    PitchEstimator pitchEstimator {N};
    float previousPowers[3] = {0x1p-32,0x1p-32,0x1p-32};
    array<uint> keyEstimations;
    uint currentKey = 0;

    Notch notch1 {1*50./rate, 1./12}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./12}; // Notch filter to remove 150Hz noise

    const uint kMax = rate / (440*exp2(-4 - 0./12 - (1./2 / 12))); // ~27 Hz ~ half pitch under A-1 (k ~ 3593 samples at 96 kHz)
    //const uint fMin = (N*50+rate/2)/rate; // ~53 Hz > 50Hz (electromagnetic noise (autocorrelation matches lower tones))
    const uint fMin  = N*440*exp2(-4 -  0./12 -   (1./2 / 12))/rate; // ~27 Hz ~ half pitch under A-1
    const uint fMax = N*440*exp2( 3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half pitch over C7 (k ~ 22 at 96 kHz)

    uint lastKey = highKey; //+1
    array<String> results; int lastLog=0; /*int lastFail=-2;*/ uint success = 0, fail=0, tries = 0, total=0;
    Time totalTime;

    uint t=0;

    PitchEstimation() {
        assert_(rate==96000, audio.rate);
        assert_(audio.channels==2, audio.channels);
        assert_(((stereo.size/2+rate/2)/rate+3)/5==uint(highKey-lowKey+1));
        //writeWaveFile("all.wav"_,stereo, audio.rate, audio.channels);

        /*this->harmonic[0].first = 1;
        this->harmonic[0].last = 11;
        this->harmonic[0].smooth = -1;
        this->harmonic[1].first = 2;
        this->harmonic[1].last = 11;
        this->harmonic[1].smooth = -1;
        this->harmonic[2].first = 2;
        this->harmonic[2].last = 10;
        this->harmonic[2].smooth = -1;
        this->harmonic[3].first = 3;
        this->harmonic[3].last = 11;
        this->harmonic[3].smooth = -1;
        this->harmonic[4].first = 3;
        this->harmonic[4].last = 10;
        this->harmonic[4].smooth = -1;*/

        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();});
        window.localShortcut(Key(' ')).connect(this, &PitchEstimation::next);

        next();
    }
    ~PitchEstimation() {
        log("-", fail, "~", success,"["_+dec(round(100.*success/tries))+"%]"_,"/",tries, "["_+dec(round(100.*tries/total))+"%]"_, "/"_,total,
            "~", "["_+dec(round(100.*success/total))+"%]\t"_+
            dec(round((float)totalTime))+"s "_+str(round(stereo.size/2./rate))+"s "_+str((stereo.size/2*1000/rate)/((uint64)totalTime))+" xRT"_
            );
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
                //x = (x + 15*notch1(x))/16; x = (x + 15*notch3(x))/16;
                signal[N-periodSize+i] = x;
            }

            uint expectedKey = highKey - t/rate/5; // Recorded one key every 5 seconds from high key to low key
            if(lastKey > expectedKey+1) {
                log("Missed key", strKey(expectedKey+1));
                for(string result: results.slice(max<int>(lastLog,results.size-256))) log(result);
                break;
            }

            float f = pitchEstimator.estimate(signal, fMin, fMax);
            float power = pitchEstimator.power;
            assert_(power<1, power);
            //uint key = round(pitchToKey(rate/k));
            uint key = round(pitchToKey(f*rate/N));

            /*float expectedK = rate/keyToPitch(key);
            const float kOffset = 12*log2(expectedK/k);
            const float kError = 12*log2((expectedK+1)/expectedK);*/

            float expectedF = keyToPitch(key)*N/rate;
            //float f = pitchEstimator.fPeak / pitchEstimator.period;
            const float fOffset =  12*log2(f/expectedF);
            const float fError =  12*log2((expectedF+1)/expectedF);

            const uint maxCountThreshold = 3;
            if(keyEstimations.size>=maxCountThreshold) keyEstimations.take(0);
            keyEstimations << key;
            map<uint,uint> count; uint maxCount=0;
            for(uint key: keyEstimations) { count[key]++; maxCount = max(maxCount, count[key]); }
            uint second=0; for(uint key: keyEstimations) if(count[key]<maxCount) second = max(second, count[key]);
            array<int> maxKeys; for(int key: keyEstimations) if(count[key]==maxCount) maxKeys << key; // Keeps most frequent keys
            currentKey = maxKeys.last(); // Resolve ties by taking last (most recent)

            results << dec((t/rate)/60,2)+":"_+dec((t/rate)%60,2)+"\t"_+strKey(expectedKey)+"\t"_+strKey(key)+"\t"_
                       //+str(round(rate/k))+" Hz\t"_
                       +str(round(f*rate/N))+" Hz\t"_
                       //+str(k)+" (k)\t"_
                       //+str(round((float)pitchEstimator.fPeak*rate/N))+"/"_+str(pitchEstimator.period)+" Hz\t"_
                       //+(kError<1./8 ? dec(round(100*kOffset))+"  ("_+dec(round(100*kError))+")\t"_ : ""_)
                       +(fError<1./8 ? dec(round(100*fOffset))+"  ("_+dec(round(100*fError))+")\t"_ : ""_)
                       +dec(log2(previousPowers[1]))+" "_+dec(log2(previousPowers[0]))+" "_+dec(log2(power))+"\t"_
                       +dec(round(100*previousPowers[2]/previousPowers[1]),3)+" "_
                       +dec(round(100*previousPowers[1]/previousPowers[0]),3)+" "_
                    +dec(round(100*previousPowers[0]/power),3)+"   "_
                    //+str(keyEstimations)+"\t"_
                    +str((t/rate)%5);

            if(key == currentKey
                    && maxCount>=maxCountThreshold
                    && t>rate // Skips first second
                    && power > exp2(-19)
                    && previousPowers[1] > previousPowers[0]/8
                    && previousPowers[0] > power/8
                    ) {
                lastKey = key;
                if(expectedKey==key) success++;
                else {
                    //lastFail = results.size, fail++;
                    this->frequency.key = expectedKey;
                    this->frequency.f = f;
                    this->frequency.rate = rate;
                    this->frequency.data = copy(pitchEstimator.spectrum);
                    this->period.key = expectedKey;
                    this->period.rate = rate;
                    //this->period.data = copy(pitchEstimator.autocorrelations);
                    this->period.data = copy(pitchEstimator.harmonicProducts);
                    this->period.f = f;
                    this->period.N = pitchEstimator.N;
                    //this->period.fPeak = pitchEstimator.fPeak;
                    this->period.signal = copy(ref<float>(signal));
                    this->period.spectrum = copy(pitchEstimator.spectrum);
                    /*for(int i: range(5)) {
                        this->harmonic[i].key = expectedKey;
                        this->harmonic[i].rate = rate;
                        this->harmonic[i].N = N;
                        this->harmonic[i].f = f;
                        this->harmonic[i].data = copy(pitchEstimator.spectrum);
                    }*/
                    for(string result: results.slice(max<int>(lastLog,results.size-256))) log(result);
                    /*{uint k=expectedKey;
                        log(strKey(k)+"\t"_+dec(round(keyToPitch(k)))+" Hz\t"_+
                            dec(round(keyToPitch(k)*N/rate))+" N\t"_+
                            dec(round(rate/keyToPitch(k)))+"k"_);}
                    {uint k=key;
                        log(strKey(k)+"\t"_+dec(round(keyToPitch(k)))+" Hz\t"_+
                            dec(round(keyToPitch(k)*N/rate))+" N\t"_+
                            dec(round(rate/keyToPitch(k)))+"k"_);}*/
                    break;
                }
                tries++;
                /*if(lastFail >= int(results.size)-2) { // Logs context
                    for(string result: results.slice(max<int>(lastLog,results.size-256))) log(result);
                    lastLog = results.size;
                }
                if(fail > 16) break;*/

                //offsets.insertMulti(key-42, 100*(fError<kError ? fOffset : kOffset));
            }
            total++;

            previousPowers[2] = previousPowers[1];
            previousPowers[1] = previousPowers[0];
            previousPowers[0] = power;
        }
        /*if(offsets) {Plot plot;
            plot.xlabel = String("Key"_), plot.ylabel = String("Cents"_);
            plot.dataSets << move(offsets);
            plots << move(plot);
        }*/
        /*if(spectrumPlot) {Plot plot;
            plot.title = String("Log spectrum"_);
            plot.xlabel = String("Frequency"_), plot.ylabel = String("Energy"_);
            plot.dataSets << copy(spectrumPlot);
            plot.log[0] = true; //plot.log[1] = true;
            plots << move(plot);
        }*/
        if(frequency.data || period.data) {
            window.setTitle(strKey(period.key));
            window.show();
            window.render();
        }
    }
} app;
