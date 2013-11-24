#include "thread.h"
#include "math.h"
#include "time.h"
#include <fftw3.h> //fftw3f
#include "pitch.h"
#include "sequencer.h"
#include "audio.h"
#include "display.h"
#include "text.h"
#include "layout.h"
#include "window.h"
#include "profile.h"
#include "biquad.h"
#if __x86_64
#define TEST 1
#include "ffmpeg.h"
#endif

struct SpectrumPlot : Widget {
    const bool logx, logy; // Whether to use log scale on x/y axis
    const float resolution; // Resolution in bins / Hz
    uint iMin = 0, iMax = 0; // Displayed range in bins
    ref<float> spectrum; // Displayed power spectrum
    // Additional analysis data
    const PitchEstimator& estimator;
    float expectedF = 0;

    SpectrumPlot(bool logx, bool logy, float resolution, ref<float> spectrum, const PitchEstimator& estimator):
        logx(logx),logy(logy),resolution(resolution),spectrum(spectrum),estimator(estimator){}
    float x(float f) { return logx ? (log2(f)-log2((float)iMin))/(log2((float)iMax)-log2((float)iMin)) : (float)(f-iMin)/(iMax-iMin); }
    void render(int2 position, int2 size) {
        if(!spectrum || iMin >= iMax) return;
        assert_(iMin <= iMax && iMax <= spectrum.size);
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

        const auto& candidate = estimator.candidates.last();
        vec4 color(1,0,0,1);
        for(uint n: range(candidate.peaks.size)) {
            uint f = candidate.peaks[n];
            float x = this->x(f+0.5)*size.x;
            line(position.x+x,position.y,position.x+x,position.y+size.y, vec4(color.xyz(),1.f/2));
        }

        for(uint i=1; i<=5; i+=2){
            const float bandwidth = 1./(i*12);
            float x0 = this->x(i*50.*resolution*exp2(-bandwidth))*size.x;
            float x1 = this->x(i*50.*resolution*exp2(+bandwidth))*size.x;
            fill(position.x+floor(x0),position.y,position.x+ceil(x1),position.y+size.y, vec4(1,1,0,1./2));
        }
    }
};

/// Estimates fundamental frequency (~pitch) of audio input
struct Tuner : Poll {
    // Static parameters
    static constexpr uint N = 32768; // Analysis window size (A0 (27Hz~4K) * 2 (flat top window) * 2 (periods) * 2 (Nyquist))
    const uint periodSize = 4096; // Overlaps to increase time resolution (compensates loss from Hann window (which improves frequency resolution))

    // Input
    Thread thread; // Audio thread to buffer periods (when kernel driver buffer was configured too low)
    AudioInput input{{this,&Tuner::write}, 96000, periodSize, thread};
    const uint rate = input.rate;

#if TEST
    //Audio audio = decodeAudio("/Samples/A0-B2.flac"_);
    Audio audio = decodeAudio("/Samples/A6-A7.flac"_);
    Timer timer {thread};
    Time realTime;
    Time totalTime;
    float frameTime = 0;
#endif

    // Input-dependent parameters
    float absoluteThreshold = 3; // 1/x, Absolute harmonic energy in the current period (i.e over mean period energy)
    float relativeThreshold = 7; // 1/x, Relative harmonic energy (i.e over current period energy)

    // A large buffer is preferred as overflows would miss most recent frames (and break the ring buffer time continuity)
    // Whereas explicitly skipping periods in processing thread skips the least recent frames and thus only misses the intermediate update
    buffer<int32> raw {2*2*N}; // Ring buffer storing unfiltered stereo samples to be recorded
    buffer<float> signal {2*N}; // Ring buffer
    uint writeIndex = 0, readIndex = 0;
    Semaphore readCount {0}; // Audio thread releases input samples, processing thread acquires
    Semaphore writeCount {(int64)signal.size}; // Processing thread releases processed samples, audio thread acquires
    uint periods=0, frames=0, skipped=0; Time lastReport; // For average overlap statistics report

    // Filter
    Notch notch1 {1*50./rate, 1./(1*12)}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./(3*12)}; // Notch filter to remove 150Hz noise
    Notch notch5 {5*50./rate, 1./(5*12)}; // Notch filter to remove 250Hz noise

    // Analysis
    PitchEstimator estimator {N};

    // Result
    float signalMaximum = 0x1p31; //1; //0x1p31; // 31bit range (1bit sign) + stereo - 3dB headroom
    uint worstKey = 0;

    // UI
    const uint textSize = 64;
    Text key {""_, textSize, white};
    Text pitch {""_, textSize, white};
    Text fOffset {""_, textSize, white};
    Text offsetMean {""_, textSize/2, white};
    Text offsetMax {""_, textSize/2, white};
    Text B {""_, textSize, white};
    Text fError {""_, textSize/3, white};
    Text absolute {""_, textSize/4, white};
    Text relative {""_, textSize/4, white};
    SpectrumPlot spectrum {false, true, (float)N/rate, estimator.spectrum, estimator};
    HBox status {{&key,&pitch,&fOffset,&offsetMean, &offsetMax, &B, &fError, &absolute,&relative}};
    OffsetPlot profile;
    VBox layout {{&spectrum, &status, &profile}};
    Window window{&layout, int2(1024,600), "Tuner"};

    Tuner() {
        log(__TIME__, input.sampleBits, input.rate, input.periodSize);
        map<string,string> args;
        for(string arg: arguments()) { string key=section(arg,'='), value=section(arg,'=',1); if(key) args.insert(key, value); }
        if(args.contains("abs"_)) { absoluteThreshold=toInteger(args.at("abs"_)); log("Absolute threshold: ", absoluteThreshold); }
        if(args.contains("rel"_)) { relativeThreshold=toInteger(args.at("rel"_)); log("Relative threshold: ", relativeThreshold); }

        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();}); //FIXME: threads waiting on semaphores will be stuck
        window.show();
#if TEST
        timer.timeout.connect(this, &Tuner::feed);
        timer.setRelative(1);
        assert_(audio.rate == input.rate, audio.rate, input.rate);
#else
        input.start();
#endif
        thread.spawn();
    }
#if TEST
    uint t = 2*rate; // Let input settle
    void feed() {
        const uint size = periodSize;
        if(t+size > audio.data.size/2) { exit(); return; }
        const int32* period = audio.data + t*2;
        write(period, size);
        t += size;
        float time = ((float)size/rate) / (float)realTime; // Instant playback rate
        const float alpha = 1./16; frameTime = (1-alpha)*frameTime + alpha*(float)time; // IIR Smoother
        realTime.reset();
        timer.setRelative(size*1000/rate/8); // 8xRT
    }
#endif
    uint write(const int32* input, uint size) {
        writeCount.acquire(size); // Will overflow if processing thread doesn't follow
        assert(writeIndex+size<=signal.size);
        for(uint i: range(size)) {
            raw[2*(writeIndex+i)+0] = input[i*2+0];
            raw[2*(writeIndex+i)+1] = input[i*2+1];
            float L = input[i*2+0], R = input[i*2+1];
            signalMaximum=::max(signalMaximum, abs(L+R));
            float x = (L+R) / signalMaximum;
            x = notch1(x);
            x = notch3(x);
            x = notch5(x);
            signal[writeIndex+i] = x;
        }
        writeIndex = (writeIndex+size)%signal.size; // Updates ring buffer pointer
        readCount.release(size); // Releases new samples
        queue(); // Queues processing thread
        return size;
    }
    void event() {
        if(readCount>2*periodSize) { // Skips frames (reduces overlap) when lagging too far behind input
            readCount.acquire(periodSize); periods++; skipped++;
            readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
            writeCount.release(periodSize); // Releases free samples
            if((float)lastReport > 1) { // Limits report rate
                log("Skipped",skipped,"periods, total",periods-frames,"from",periods,"-> Average overlap", 1 - (float) (periods * periodSize) / (frames * N));
                lastReport.reset(); skipped=0;
            }
        }
        readCount.acquire(periodSize); periods++;
        frames++;
        buffer<float> frame {N}; // Need a linear buffer for autocorrelation (FIXME: mmap ring buffer)
        assert(readIndex+periodSize<=signal.size);
        for(uint i: range(min<int>(N,signal.size-readIndex))) frame[i] = signal[readIndex+i]; // Copies ring buffer head into linear frame buffer
        for(uint i: range(signal.size-readIndex, N)) frame[i] = signal[i+readIndex-signal.size]; // Copies ring buffer tail into linear frame buffer

        float f = estimator.estimate(frame);
        int key = round(pitchToKey(f*rate/N));

        spectrum.iMin = keyToPitch(key-1./2)*N/rate;
        spectrum.iMax = min(N/4, uint(16*keyToPitch(key+1./2)*N/rate));

        float meanPeriodEnergy = estimator.meanPeriodEnergy; // Stabilizes around 8 (depends on FFT size, energy, range, noise, signal, ...)
        float absolute = estimator.harmonicEnergy / meanPeriodEnergy;

        float periodEnergy = estimator.periodEnergy;
        float relative = estimator.harmonicEnergy  / periodEnergy;

        if(absolute > 1./(absoluteThreshold*2) && relative > 1./(relativeThreshold*2)) {
            float expectedF = keyToPitch(key)*N/rate;
            const float offset =  12*log2(f/expectedF);
            const int cOffset =  round(100*offset);
            const float error =  12*log2((expectedF+1)/expectedF);

            this->key.setText(strKey(key));
            this->pitch.setText(dec(round(f*rate/N )));
            this->fOffset.setText(dec(round(100*offset)));
            {int offset = round(100*12*log2(estimator.energyWeightedF/expectedF));
                this->offsetMean.setText(abs(offset)<100&&offset!=cOffset?dec(offset):""_);}
            {int offset = round(100*12*log2(estimator.peakF/expectedF));
                this->offsetMax.setText(abs(offset)<100&&offset!=cOffset?dec(offset):""_);}
            this->B.setText(dec(round(100*12*log2(1+estimator.B))));
            this->fError.setText(dec(round(100*error)));
            this->absolute.setText(dec(round(1./absolute)));
            this->relative.setText(dec(round(1./relative)));

            if(absolute > 1./absoluteThreshold && relative > 1./relativeThreshold && key>=21 && key<21+keyCount) {
                float& keyOffset = profile.offsets[key-21]; {const float alpha = 1./4; // Prevents mistuned neighbouring note from affecting wrong key
                        keyOffset = (1-alpha)*keyOffset + alpha*offset;} // Smoothes offset changes
                float variance = sq(offset - keyOffset);
                float& keyVariance = profile.variances[key-21];
                {const float alpha = 1./8; keyVariance = (1-alpha)*keyVariance + alpha*variance;} // Smoothes deviation changes
                {uint k = this->worstKey;
                    for(uint i: range(7,keyCount)) //FIXME: quadratic, cubic, exp curve ?
                        if(  min(abs(profile.offsets[i ]), abs(profile.offsets[i ] - 1.f/8 * (float)(i -keyCount/2)/(keyCount/2))) /*+ sqrt(profile.variances[i ])*/ >
                             min(abs(profile.offsets[k]), abs(profile.offsets[k] - 1.f/8 * (float)(k-keyCount/2)/(keyCount/2))) /*+ sqrt(profile.variances[k])*/ ) k = i;
                    if(k != this->worstKey) { this->worstKey=k; window.setTitle(strKey(21+k)); }
                }
            }
        }

        readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
        writeCount.release(periodSize); // Releases free samples (only after having possibly recorded the whole ring buffer)
        window.render();
    }
} app;


