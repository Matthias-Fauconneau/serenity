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
        float sMin = estimator.periodPower;

        {float y = (logy ? (log2(2*sMin) - log2(sMin)) / (log2(sMax)-log2(sMin)) : (2*sMin / sMax)) * (size.y-12);
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
    Audio audio = decodeAudio("/Samples/"_+arguments()[0]+"-"_+arguments()[1]+".flac"_);
    Timer timer {thread};
#endif

    // A large buffer is preferred as overflows would miss most recent frames (and break the ring buffer time continuity)
    // Whereas explicitly skipping periods in processing thread skips the least recent frames and thus only misses the intermediate update
    buffer<float> signal {2*N}; // Ring buffer
    uint writeIndex = 0, readIndex = 0;
    Semaphore readCount {0}; // Audio thread releases input samples, processing thread acquires
    Semaphore writeCount {(int64)signal.size}; // Processing thread releases processed samples, audio thread acquires
    uint periods=0, frames=0, skipped=0; Time lastReport; // For average overlap statistics report

    // Analysis
    float confidencethreshold = 6; // 1/ Relative harmonic energy (i.e over current period energy)
    float ambiguityThreshold = 16; // 1-1/ Energy of second candidate relative to first
    PitchEstimator estimator {N};
    int lastKey = 0;
    float lastOffset, lastConfidence;
    int worstKey = -1;

    // UI
    const uint textSize = 64;
    Text key {""_, textSize, white};
    Text pitch {""_, textSize, white};
    Text fOffset {""_, textSize, white};
    Text B {""_, textSize/2, white};
    Text fError {""_, textSize/3, white};
    Text confidence {""_, textSize/4, white};
    HBox status {{&key,&pitch,&fOffset, &B, &fError, &confidence}};
    OffsetPlot profile;
    VBox layout {{&status, &profile}};
    Window window{&layout, int2(1024,600), "Tuner"};

    Tuner() {
        log(__TIME__, input.sampleBits, input.rate, input.periodSize);
        if(arguments().size>0 && isInteger(arguments()[0])) confidencethreshold=toInteger(arguments()[0]);
        if(arguments().size>1 && isInteger(arguments()[1])) ambiguityThreshold=toInteger(arguments()[1]);

        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();}); //FIXME: threads waiting on semaphores will be stuck
        window.show();
#if TEST
        timer.timeout.connect(this, &Tuner::feed);
        timer.setRelative(1);
        assert_(audio.rate == input.rate, audio.rate, input.rate);
        profile.reset();
#else
        input.start();
#endif
        thread.spawn();
        readCount.acquire(N-periodSize);
    }
#if TEST
    uint t = 0;// 5*rate; // Let input settle
    void feed() {
        const uint size = periodSize;
        if(t+size > audio.data.size/2) { return; }
        const int32* period = audio.data + t*2;
        write(period, size);
        t += size;
        timer.setRelative(size*1000/rate/8); // 8xRT
    }
#endif
    uint write(const int32* input, uint size) {
        writeCount.acquire(size); // Will overflow if processing thread doesn't follow
        assert(writeIndex+size<=signal.size);
        for(uint i: range(size)) {
            float L = input[i*2+0];//, R = input[i*2+1];
            float x = (L/*+R*/) * 0x1p-31;
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
        assert(readIndex+periodSize<=signal.size);
        for(uint i: range(min<int>(N,signal.size-readIndex))) estimator.windowed[i] = estimator.window[i] * signal[readIndex+i];
        for(uint i: range(signal.size-readIndex, N)) estimator.windowed[i] = estimator.window[i] * signal[i+readIndex-signal.size];

        float f = estimator.estimate();
        float confidence = estimator.harmonicEnergy  / estimator.periodEnergy;
        assert(estimator.candidates.size==2);
        float ambiguity = estimator.candidates[1].key ? estimator.candidates[0].key / estimator.candidates[1].key : 0;

        if(confidence > 1./(confidencethreshold+1) && 1-ambiguity >= 0) {
            int key = round(pitchToKey(f*rate/N));
            float expectedF = keyToPitch(key)*N/rate;
            const float offset =  12*log2(f/expectedF);
            const float error =  12*log2((expectedF+1)/expectedF);

            this->key.setText(strKey(key));
            this->pitch.setText(dec(round(f*rate/N )));
            this->fOffset.setText(dec(round(100*offset)));
            this->B.setText(dec(round(100*12*log2(1+estimator.B))));
            this->fError.setText(dec(round(100*error)));
            this->confidence.setText(dec(round(1./confidence)));

            if(key==lastKey) {
                const float alpha = lastConfidence; //1./8 | confidence
                float& keyOffset = profile.offsets[key-21]; keyOffset = (1-alpha)*keyOffset + alpha*lastOffset;
                float& keyVariance = profile.variances[key-21]; keyVariance = (1-alpha)*keyVariance + alpha*sq(lastOffset - keyOffset);
                {int k = this->worstKey;
                    for(uint i: range(keyCount)) //FIXME: quadratic, cubic, exp curve ?
                        if(  k<0 ||
                                abs(profile.offsets[i ] - stretch(i)) /*+ sqrt(profile.variances[i ])*/ >
                             abs(profile.offsets[k] - stretch(k)) /*+ sqrt(profile.variances[k])*/ ) k = i;
                    if(k != this->worstKey) { this->worstKey=k; window.setTitle(strKey(21+k)); }
                }
            }
            // Delay effect (Swap both tests for immediate effect)
            if(confidence > 1./confidencethreshold && 1-ambiguity > 1./ambiguityThreshold && key>=21 && key<21+keyCount) {
                lastKey = key; lastConfidence=confidence, lastOffset=offset; // Delays offset effect until key change confirmation
                //FIXME: last lastKey is never
            }
        }

        readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
        writeCount.release(periodSize); // Releases free samples (only after having possibly recorded the whole ring buffer)
        window.render();
    }
} app;


