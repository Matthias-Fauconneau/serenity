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
#if __x86_64
#define TEST 1
#include "ffmpeg.h"
#endif

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
    float confidenceThreshold = 9; // 1/ Relative harmonic energy (i.e over current period energy)
    float ambiguityThreshold = 21; // 1-1/ Energy of second candidate relative to first
    float threshold = 25; // Product of confidence and ambiguity

    PitchEstimator estimator {N};
    int lastKey = -1;
    float keyOffset = 0;
    bool record = true;
    int worstKey = 0;

    // UI
    const uint textSize = 64;
    Text key {""_, textSize, white};
    Text fOffset {""_, textSize, white};
    Text B {""_, textSize/2, white};
    HBox status {{&key, &fOffset, &B}};
    OffsetPlot profile;
    VBox layout {{&status, &profile}};
    Window window{&layout, int2(1024,600), "Tuner"};

    Tuner() {
        log(__TIME__, input.sampleBits, input.rate, input.periodSize);
        if(arguments().size>0 && isInteger(arguments()[0])) confidenceThreshold=toInteger(arguments()[0]);
        if(arguments().size>1 && isInteger(arguments()[1])) ambiguityThreshold=toInteger(arguments()[1]);

        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();}); //FIXME: threads waiting on semaphores will be stuck
        window.localShortcut(Key(' ')).connect([this]{record=!record;}); //FIXME: threads waiting on semaphores will be stuck
        window.show();
#if TEST
        timer.timeout.connect(this, &Tuner::feed);
        timer.setRelative(1);
        assert_(audio.rate == input.rate, audio.rate, input.rate);
        //profile.reset();
#else
        input.start();
#endif
        thread.spawn();
        readCount.acquire(N-periodSize);
    }
#if TEST
    uint t = 0;
    void feed() {
        const uint size = periodSize;
        if(t+size > audio.data.size/2) { exit(); return; }
        const int32* period = audio.data + t*2;
        write(period, size);
        t += size;
        timer.setRelative(size*1000/rate/8); // 8xRT
    }
#endif
    uint write(const int32* input, uint size) {
        writeCount.acquire(size); // Will overflow if processing thread doesn't follow
        assert(writeIndex+size<=signal.size);
        for(uint i: range(size)) signal[writeIndex+i] = input[i*2+0] * 0x1p-24;
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
        float ambiguity = estimator.candidates.size==2 && estimator.candidates[1].key
                && estimator.candidates[0].f0*(1+estimator.candidates[0].B)!=f ?
                    estimator.candidates[0].key / estimator.candidates[1].key : 0;

        if(confidence > 1./confidenceThreshold/2 && 1-ambiguity > 1./ambiguityThreshold/2 && confidence*(1-ambiguity) > 1./threshold/2) {
            int key = round(pitchToKey(f*rate/N));
            float expectedF = keyToPitch(key)*N/rate;
            const float offset =  12*log2(f/expectedF);

            if(record && confidence >= 1./confidenceThreshold && 1-ambiguity > 1./ambiguityThreshold && confidence*(1-ambiguity) > 1./threshold
                    && key>=21 && key<21+keyCount) {
                const float alpha = 1./16;
                float& keyOffset = profile.offsets[key-21]; keyOffset = (1-alpha)*keyOffset + alpha*offset;
                float& keyVariance = profile.variances[key-21]; keyVariance = (1-alpha)*keyVariance + alpha*sq(offset - keyOffset);
                {int k = this->worstKey;
                    for(uint i: range(3, keyCount-2))
                        if(  k<0 ||
                             abs(profile.offsets[i ] - stretch(i)) /*+ sqrt(profile.variances[i ])*/ >
                             abs(profile.offsets[k] - stretch(k)) /*+ sqrt(profile.variances[k])*/ ) k = i;
                    if(k != this->worstKey) { this->worstKey=k; window.setTitle(strKey(21+k)); }
                }
            }

            if(key!=lastKey) keyOffset = offset; // Resets on key change
            keyOffset = (keyOffset+offset)/2; // Running average
            this->key.setText(strKey(key));
            this->fOffset.setText(dec(round(100*keyOffset)));
            const int B = round(100*12*log2(1+estimator.B));
            this->B.setText(B?dec(B):strKey(21+worstKey));
        }

        readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
        writeCount.release(periodSize); // Releases free samples (only after having possibly recorded the whole ring buffer)
        window.render();
    }
} app;


