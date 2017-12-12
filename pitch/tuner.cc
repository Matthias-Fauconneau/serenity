#include "thread.h"
#include "math.h"
#include "time.h"
#include "pitch.h"
#include "asound.h"
#include "graphics.h"
#include "text.h"
#include "ui/layout.h"
#include "window.h"
#include "profile.h"

/// Estimates fundamental frequency (~pitch) of audio input
struct Tuner : Poll {
    // Static parameters
    static constexpr uint rate = 48000;
    static constexpr uint N = 32768; // Analysis window size (A0 (27Hz~4K) * 2 (flat top window) * 2 (periods) * 2 (Nyquist))
    const uint periodSize = 4096; // Overlaps to increase time resolution (compensates loss from Hann window (which improves frequency resolution))

    // Input
    Thread thread {-20}; // Audio thread to buffer periods (when kernel driver buffer was configured too low)
	AudioInput input{{this,&Tuner::write}, thread};

    // A large buffer is preferred as overflows would miss most recent frames (and break the ring buffer time continuity)
    // Whereas explicitly skipping periods in processing thread skips the least recent frames and thus only misses the intermediate update
    buffer<float> signal {2*N}; // Ring buffer
    uint writeIndex = 0, readIndex = 0;
    Semaphore readCount {0}; // Audio thread releases input samples, processing thread acquires
    Semaphore writeCount {(int64)signal.size}; // Processing thread releases processed samples, audio thread acquires
    uint periods=0, frames=0, skipped=0; Time lastReport {true}; // For average overlap statistics report

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
	Lock lock;
    int currentKey = 0;
    Text currentKeyText {"", 64, white};
    int keyOffsetCents = 0;
    Text keyOffsetText {"", 64, white};
    int worstKey = 0;
    Text worstKeyText {"", 64, white};
    HBox status {{&currentKeyText, &keyOffsetText, &worstKeyText}, HBox::Even};
    OffsetPlot profile;
    VBox layout {{&status, &profile}};
	//Thread uiThread;
    unique<Window> window = ::window(&layout, 0);

    Tuner() {
		if(arguments().size>0 && isInteger(arguments()[0])) minWorstKey=parseInteger(arguments()[0]);
		if(arguments().size>1 && isInteger(arguments()[1])) maxWorstKey=parseInteger(arguments()[1]);

        window->backgroundColor = black;
        window->actions[Space] = [this]{record=!record;}; //FIXME: threads waiting on semaphores will be stuck

		input.start(2, rate, periodSize);
		log(input.sampleBits, input.rate, input.periodSize);
        thread.spawn();
        readCount.acquire(N-periodSize);
        window->show();
    }

	uint write(const ref<int32> input) {
		if(!writeCount.tryAcquire(input.size/2)) {
			log("Overflow", writeCount, input.size/2, readCount);
			writeCount.acquire(input.size/2); // Will overflow if processing thread doesn't follow
        }
		assert(writeIndex+input.size/2<=signal.size);
		for(uint i: range(input.size/2)) signal[writeIndex+i] = input[i*2] * 0x1p-24; // Use left channel only
		writeIndex = (writeIndex+input.size/2)%signal.size; // Updates ring buffer pointer
		readCount.release(input.size/2); // Releases new samples
        queue(); // Queues processing thread
		return input.size/2;
    }

    void event() {
		if(readCount>uint64(2*periodSize)) { // Skips frames (reduces overlap) when lagging too far behind input
            readCount.acquire(periodSize); periods++; skipped++;
            readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
            writeCount.release(periodSize); // Releases free samples
            if(lastReport.nanoseconds() > second) { // Limits report rate
                log("Skipped",skipped,"periods, total",periods-frames,"from",periods,"-> Average overlap", 1 - (float) (periods * periodSize) / (frames * N));
                lastReport.reset(); skipped=0;
            }
        }
        if(!readCount.tryAcquire(periodSize)) {
            readCount.acquire(periodSize);
        }
        periods++;
        frames++;
        assert(readIndex+periodSize<=signal.size);
        for(uint i: range(min<int>(N,signal.size-readIndex))) estimator.windowed[i] = estimator.window[i] * signal[readIndex+i];
        for(uint i: range(signal.size-readIndex, N)) estimator.windowed[i] = estimator.window[i] * signal[i+readIndex-signal.size];
        readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
        writeCount.release(periodSize); // Releases free samples (only after having possibly recorded the whole ring buffer)

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
            bool needUpdate = false;
            if(currentKey!=key) {
                currentKey = key;
                currentKeyText = Text(strKey(key), 64, white);
                needUpdate = true;
            }
            if(key!=lastKey) keyOffset = offset; // Resets on key change
            keyOffset = (keyOffset+offset)/2; // Running average
            int keyOffsetCents = round(100*(keyOffset - stretch(key-21)*12));
            if(this->keyOffsetCents != keyOffsetCents) {
                this->keyOffsetCents = keyOffsetCents;
				keyOffsetText = Text(str(keyOffsetCents), 32, white);
                needUpdate = true;
            }

            if(record && confidence >= 1./confidenceThreshold && 1-ambiguity > 1./ambiguityThreshold && confidence*(1-ambiguity) > 1./threshold
                    && key>=21 && key<21+keyCount) {
                const float alpha = 1./16;
                float& keyOffset = profile.offsets[key-21]; keyOffset = (1-alpha)*keyOffset + alpha*offset;
                float& keyVariance = profile.variances[key-21]; keyVariance = (1-alpha)*keyVariance + alpha*sq(offset - keyOffset);
                int k = -1;
                for(uint i: range(minWorstKey, maxWorstKey)) if(k<0 || abs(profile.offsets[i] - stretch(i)*12) > abs(profile.offsets[k] - stretch(k)*12)) k = i;
                int worstKey = 21+k;
                if(this->worstKey != worstKey) {
                    this->worstKey = worstKey;
                    worstKeyText = Text(strKey(worstKey), 64, white);
                }
                int keyOffsetCents = round(100*(keyOffset- stretch(key-21)*12));
                if(this->keyOffsetCents != keyOffsetCents) {
                    this->keyOffsetCents = keyOffsetCents;
					keyOffsetText = Text(str(keyOffsetCents), 64, white);
                    needUpdate = true;
                }
            }
            if(needUpdate) window->render();
        }
    }
} app;
