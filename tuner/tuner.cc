#include "thread.h"
#include "math.h"
#include "time.h"
#include <fftw3.h> //fftw3f
#include "pitch.h"
#include "audio.h"
#include "graphics.h"
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
    const uint lowKey=parseKey(arguments().value(0,"A0"))-12, highKey=parseKey(arguments().value(1,"A7"_))-12;
    AudioFile audio {"/Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_};
    Timer timer {{this, &Tuner::feed}, 1, thread};
#endif

    // A large buffer is preferred as overflows would miss most recent frames (and break the ring buffer time continuity)
    // Whereas explicitly skipping periods in processing thread skips the least recent frames and thus only misses the intermediate update
    buffer<float> signal {2*N}; // Ring buffer
    uint writeIndex = 0, readIndex = 0;
    Semaphore readCount {0}; // Audio thread releases input samples, processing thread acquires
    Semaphore writeCount {(int64)signal.size}; // Processing thread releases processed samples, audio thread acquires
    uint periods=0, frames=0, skipped=0; Time lastReport; // For average overlap statistics report

    // Analysis
    float confidenceThreshold = 10; // 1/ Relative harmonic energy (i.e over current period energy)
    float ambiguityThreshold = 21; // 1-1/ Energy of second candidate relative to first
    float threshold = 24; // Product of confidence and ambiguity

    PitchEstimator estimator {N};
    int lastKey = -1;
    float keyOffset = 0;
    bool record = true;
    int minWorstKey = 1, maxWorstKey = keyCount;

    // UI
    Text currentKey {""_, 64, white};
    Text fOffset {""_, 64, white};
    Text worstKey {""_, 64, white};
    HBox status {{&currentKey, &fOffset, &worstKey}, HBox::Even};
    OffsetPlot profile;
    VBox layout {{&status, &profile}};
    Window window{&layout, int2(1024,600), "Tuner"};

    Tuner() {
        log(__TIME__, input.sampleBits, input.rate, input.periodSize);
        if(arguments().size>0 && isInteger(arguments()[0])) minWorstKey=fromInteger(arguments()[0]);
        if(arguments().size>1 && isInteger(arguments()[1])) maxWorstKey=fromInteger(arguments()[1]);

        window.background = Window::Black;
        window.actions[Escape] = []{exit();}; //FIXME: threads waiting on semaphores will be stuck
        window.actions[Space] = [this]{record=!record;}; //FIXME: threads waiting on semaphores will be stuck
        window.show();
#if TEST
        assert(audio.rate == input.rate);
        //profile.reset();
#endif
        thread.spawn();
        readCount.acquire(N-periodSize);
    }
#if TEST
    uint t = 0;
    void feed() {
        buffer<int2> period (periodSize);
        if(audio.read(period) < period.size) { audio.close(); exit(); return; }
        write(period);
        t += period.size;
        timer.setRelative(period.size*1000/rate/8); // 8xRT
    }
#endif
    uint write(const ref<int2>& input) {
        writeCount.acquire(input.size); // Will overflow if processing thread doesn't follow
        assert(writeIndex+input.size<=signal.size);
        for(uint i: range(input.size)) signal[writeIndex+i] = input[i][0] * 0x1p-24; // Left channel only
        writeIndex = (writeIndex+input.size)%signal.size; // Updates ring buffer pointer
        readCount.release(input.size); // Releases new samples
        queue(); // Queues processing thread
        return input.size;
    }
    void event() {
        if(readCount>int(2*periodSize)) { // Skips frames (reduces overlap) when lagging too far behind input
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
        int key = f > 0 ? round(pitchToKey(f*rate/N)) : 0;
        float expectedF = f > 0 ? keyToPitch(key)*N/rate : 0;
        const float offset = f > 0 ? 12*log2(f/expectedF) : 0;

        float confidenceThreshold = this->confidenceThreshold;
        float ambiguityThreshold = this->ambiguityThreshold;
        float threshold = this->threshold;
        float offsetThreshold = 1./2;
        if(f < 13) { // Stricter thresholds for ambiguous bass notes
            threshold = 21;
            offsetThreshold = 0.43;
        }
        if(confidence > 1./confidenceThreshold/2 && 1-ambiguity > 1./ambiguityThreshold/2 && confidence*(1-ambiguity) > 1./threshold/2
                && abs(offset)<offsetThreshold ) {

            currentKey = Text(strKey(key), 64, white);
            if(key!=lastKey) keyOffset = offset; // Resets on key change
            keyOffset = (keyOffset+offset)/2; // Running average
            fOffset = Text(dec(round(100*keyOffset)), 64, white);

            if(record && confidence >= 1./confidenceThreshold && 1-ambiguity > 1./ambiguityThreshold && confidence*(1-ambiguity) > 1./threshold
                    && key>=21 && key<21+keyCount) {
                const float alpha = 1./16;
                float& keyOffset = profile.offsets[key-21]; keyOffset = (1-alpha)*keyOffset + alpha*offset;
                float& keyVariance = profile.variances[key-21]; keyVariance = (1-alpha)*keyVariance + alpha*sq(offset - keyOffset);
                int k = -1;
                for(uint i: range(minWorstKey, maxWorstKey)) if(k<0 || abs(profile.offsets[i] - stretch(i)*12) > abs(profile.offsets[k] - stretch(k)*12)) k = i;
                worstKey = Text(strKey(21+k), 64, white);
                fOffset = Text(dec(round(100*keyOffset)), 64, white);
            }
        }

        readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
        writeCount.release(periodSize); // Releases free samples (only after having possibly recorded the whole ring buffer)
        window.render();
    }
} app;
