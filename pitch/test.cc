#include "time.h"
#include "ffmpeg.h"
#include "pitch.h"
#include "profile.h"

/// Estimates fundamental frequency (~pitch) of audio input
struct Test : Poll {
    // Static parameters
    static constexpr uint rate = 96000;
    static constexpr uint N = 32768; // Analysis window size (A0 (27Hz~4K) * 2 (flat top window) * 2 (periods) * 2 (Nyquist))
    static constexpr uint periodSize = 4096; // Overlaps to increase time resolution (compensates loss from Hann window (which improves frequency resolution))

    Thread thread {-20}; // Audio thread
    Timer timer {{this, &Test::feed}, 1, thread};
    const uint lowKey=parseKey(arguments().value(0,"A0"))-12, highKey=parseKey(arguments().value(1,"A7"_))-12;
    AudioFile audio {"/Samples/"_+strKey(lowKey+12)+"-"_+strKey(highKey+12)+".flac"_};

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

    Test() {
        assert_(audio.rate == rate);
        thread.spawn();
        readCount.acquire(N-periodSize);
    }

    uint t = 0;
    void feed() {
        buffer<int2> period (periodSize);
        if(audio.read(period) < period.size) { audio.close(); exit(); return; }
        write(period);
        t += period.size;
        timer.setRelative(period.size*1000/rate/8); // 8x real time
    }

    uint write(const ref<int2>& input) {
        if(!writeCount.tryAcquire(input.size)) {
            log("Overflow", writeCount, input.size, readCount);
            writeCount.acquire(input.size); // Will overflow if processing thread doesn't follow
        }
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
            /*if((float)lastReport > 1)*/ { // Limits report rate
                log("Skipped",skipped,"periods, total",periods-frames,"from",periods,"-> Average overlap", 1 - (float) (periods * periodSize) / (frames * N));
                lastReport.reset(); skipped=0;
                abort("Unexpected real time processing failure");
            }
        }
        if(!readCount.tryAcquire(periodSize)) {
            //log("Underflow", readCount, periodSize, writeCount);
            readCount.acquire(periodSize);
        }
        //log("process");
        periods++;
        frames++;
        assert(readIndex+periodSize<=signal.size);
        for(uint i: range(min<int>(N,signal.size-readIndex))) estimator.windowed[i] = estimator.window[i] * signal[readIndex+i];
        for(uint i: range(signal.size-readIndex, N)) estimator.windowed[i] = estimator.window[i] * signal[i+readIndex-signal.size];

        float f = estimator.estimate();
        float confidence = estimator.periodEnergy ? estimator.harmonicEnergy  / estimator.periodEnergy : 0;
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


            if(key!=lastKey) keyOffset = offset; // Resets on key change
            keyOffset = (keyOffset+offset)/2; // Running average
            log(strKey(key), dec(round(100*keyOffset)));
        }

        readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
        writeCount.release(periodSize); // Releases free samples (only after having possibly recorded the whole ring buffer)
    }
} app;

