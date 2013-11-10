#include "thread.h"
#include "math.h"
#include "pitch.h"
#include <fftw3.h> //fftw3f
//include "flac.h"
#include "ffmpeg.h"
#include "data.h"
#include "time.h"
#include "plot.h"
#include "layout.h"
#include "window.h"

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


/// Estimates fundamental frequencies (~pitches) of notes in a single file
struct PitchEstimation {
    VList<Plot> plots;
    Window window {&plots, int2(1024, 768), "Tuner"};

    PitchEstimation() {
        map<float, float> spectrumPlot;
        map<float,float> offsets;

        const int lowKey=parseKey("A0"_)-12, highKey=parseKey("C3"_)-12;
        //Audio audio = decodeAudio(Map("Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_));
        Audio audio = decodeAudio("/Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_);
        ref<int32> stereo = audio.data;
        const uint rate = audio.rate;
        assert_(rate==96000, audio.rate);
        assert_(audio.channels==2, audio.channels);
        assert_(((stereo.size/2+rate/2)/rate+3)/5==uint(highKey-lowKey+1));
        //writeWaveFile("all.wav"_,stereo, audio.rate, audio.channels);

        static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
        PitchEstimator pitchEstimator {N};
        float previousPowers[3] = {0,0,0};
        array<int> keyEstimations;
        //uint instantKey = 0;
        uint currentKey = 0;

        Notch notch1(1*50./rate, 1./12); // Notch filter to remove 50Hz noise
        //Notch notch3(3*50./rate, 1./12); // Cascaded to remove the first odd harmonic (3rd partial)

        const uint kMax = rate / (440*exp2(-4 - 0./12 - (1./2 / 12))); // ~27 Hz ~ half pitch under A-1 (k ~ 3593 samples at 96 kHz)
        const uint fMax = N*440*exp2(3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half pitch over C7 (k ~ 22 at 96 kHz)
        log(kMax, fMax);

        uint lastKey = highKey;
        array<String> results; int lastLog=0; uint lastFail=0; uint success = 0, fail=0, tries = 0, total=0;
        Time totalTime;
        for(uint t=3*N; t<=stereo.size/2-N; t+=N) {
            const int32* period = stereo + t*2;
            float signal[N];
            for(uint i: range(N)) {
                float x = (period[i*2+0]+period[i*2+1]) * 0x1p-32f; // 32 + 1 (stereo) - 1 (sign)
                /*if(abs(instantKey-pitchToKey(notch1.frequency*rate)) > 1 || previousPowers[0]<2*exp2(-15))*/ x = notch1(x); //FIXME: overcome with HPS
                //if(abs(instantKey-pitchToKey(notch3.frequency*rate)) > 1 || previousPowers[0]<2*exp2(-15)) x = notch3(x);
                signal[i] = x;
            }

            uint expectedKey = highKey - t/rate/5; // Recorded one key every 5 seconds from high key to low key

            float k = pitchEstimator.estimate(signal, kMax, fMax);
            float power = pitchEstimator.power;
            assert_(power<1, power);
            uint key = round(pitchToKey(rate/k));

            float expectedK = rate/keyToPitch(key);
            const float kOffset = 12*log2(expectedK/k);
            const float kError = 12*log2((expectedK+1)/expectedK);

            float expectedF = keyToPitch(key)*N/rate;
            float f = pitchEstimator.fPeak / pitchEstimator.period;
            const float fOffset =  12*log2(f/expectedF);
            const float fError =  12*log2((expectedF+1)/expectedF);

            if(power > exp2(-15) && previousPowers[0] > power/16) {
                //instantKey = key;
                if(keyEstimations.size>=3) keyEstimations.take(0);
                keyEstimations << key;
                map<int,int> count; int maxCount=0;
                for(int key: keyEstimations) { count[key]++; maxCount = max(maxCount, count[key]); }
                int second=0; for(int key: keyEstimations) if(count[key]<maxCount) second = max(second, count[key]);
                array<int> maxKeys; for(int key: keyEstimations) if(count[key]==maxCount) maxKeys << key; // Keeps most frequent keys
                currentKey = maxKeys.last(); // Resolve ties by taking last (most recent)

                if(key == currentKey && maxCount>=2) {
                    int pass = expectedKey==key && (key==lastKey || key+1==lastKey);
                    //if(key==expectedKey+1 && t-keyStart<1) // First second of each notes is actually Benchmark might not be accurately timed
                    results << dec((t/rate)/60,2)+":"_+dec((t/rate)%60,2)+"\t"_+strKey(expectedKey)+"\t"_+strKey(key)+"\t"_
                               +str(round(rate/k))+" Hz\t"_
                               +str(k)+" (k)\t"_
                               +str(pitchEstimator.fPeak*rate/N)+"/"_+str(pitchEstimator.period)+" Hz\t"_
                               +(kError<1./8 ? dec(round(100*kOffset))+"  ("_+dec(round(100*kError))+")\t"_ : ""_)
                               +(fError<1./8 ? dec(round(100*fOffset))+"  ("_+dec(round(100*fError))+")\t"_ : ""_)
                               +dec(log2(previousPowers[1]))+" "_+dec(log2(previousPowers[0]))+" "_+dec(log2(power))+"\t"_
                               //+dec(round(100*previousPowers[2]/previousPowers[1]),3)+" "_
                               //+dec(round(100*previousPowers[1]/previousPowers[0]),3)+" "_
                            //+dec(round(100*previousPowers[0]/power),3)+"\t"_
                            +str(keyEstimations)+"\t"_
                            +str(t/N)+"\t"_
                            +str((t/rate)%5)+"\t"_
                            +(pass ?""_ : (((t/rate)%5)<1 && key==expectedKey+1) ? "~"_ : "X"_);
                    if(!pass && !(((t/rate)%5)<1 && key==expectedKey+1)) lastFail = results.size, fail++;
                    else success++;
                    tries++;
                    if(lastFail && lastFail >= results.size-2) { // Logs context
                        for(string result: results.slice(max<int>(lastLog,results.size-8))) log(result);
                        lastLog = results.size;
                    }
                    //if(fail > 16) break;

                    offsets.insertMulti(key-42, 100*(fError<kError ? fOffset : kOffset));
                    if(expectedKey!=key && !spectrumPlot) for(uint i: range(16, 4096*N/rate)) {
                        float e = pitchEstimator.spectrum[i];
                        if(e>1./16) spectrumPlot.insert(i*rate/N, e);
                    }
                    lastKey = currentKey;
                }
            } else if(keyEstimations.size) keyEstimations.take(0);
            total++;

            previousPowers[2] = previousPowers[1];
            previousPowers[1] = previousPowers[0];
            previousPowers[0] = power;
        }
        log("-", fail, "~", success,"["_+dec(round(100.*success/tries))+"%]"_,"/",tries, "["_+dec(round(100.*tries/total))+"%]"_, "/"_,total,
            "~", "["_+dec(round(100.*success/total))+"%]\t"_+
            dec(round((float)totalTime))+"s "_+str(round(stereo.size/2./rate))+"s "_+str((stereo.size/2*1000/rate)/((uint64)totalTime))+" xRT"_
            );
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
        if(plots) {
            window.backgroundColor=window.backgroundCenter=1;
            window.show();
            window.localShortcut(Escape).connect([]{exit();});
        }
    }
} app;
