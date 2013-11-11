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
#if __x86_64
#define TEST 1
#include "ffmpeg.h"
#endif
#include "profile.h"

//FIXME: Share with test (or merge test and tuner?)
struct HarmonicPlot : Widget {
    static constexpr uint harmonics = PitchEstimator::harmonics;
    uint rate = 0;
    ref<real> harmonic;
    float fMin, fMax;
    float log(float f) { return (log2(f)-log2(fMin))/(log2(fMax)-log2(fMin)); }
    void render(int2 position, int2 size) {
        const uint N = harmonic.size*2;
        real sMax = 0;
        const uint iMin = fMin*N*harmonics/rate, iMax = ceil(fMax*N*harmonics/rate);
        for(uint i: range(iMin, iMax)) sMax = max(sMax, harmonic[i]);
        if(!sMax) return;
        real sMin = sMax;
        for(uint i: range(iMin, iMax)) if(harmonic[i]) sMin = min(sMin, harmonic[i]);
        for(uint i: range(iMin, iMax)) {
            float x0 = log((float)i*rate/N/harmonics) * size.x;
            float x1 = log((float)(i+1)*rate/N/harmonics) * size.x;
            float s =  (harmonic[i] - sMin) / (sMax-sMin);
            float y = s * (size.y-16);
            fill(position.x+x0,position.y+size.y-y,position.x+x1,position.y+size.y,white);
        }
        {float x = log((fMin+fMax)/2)*size.x;
            fill(position.x+x,position.y,position.x+x+1,position.y+size.y, vec4(0,1,0,1));}
    }
};

/// Estimates fundamental frequency (~pitch) of audio input
struct Tuner : Poll {
    // Static parameters
    static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
    const uint periodSize = 4096; // Overlaps to increase time resolution (compensates loss from Hann window (which improves frequency resolution))

    // Input
    Thread thread; // Audio thread to buffer periods (when kernel driver buffer was configured too low)
    AudioInput input{{this,&Tuner::write}, 96000, periodSize, thread};
    const uint rate = input.rate;

#if TEST
    Audio audio = decodeAudio("/Samples/A0-C3.flac"_);
    Timer timer {thread};
    Time realTime;
    Time totalTime;
    float frameTime = 0;
#endif

    // Input-dependent parameters
    const uint fMin  = N*440*exp2(-4)/rate; // ~27 Hz ~ A-1
    float ratioThreshold = 0.21;
    float powerThreshold = 11;

    // A large buffer is preferred as overflows would miss most recent frames (and break the ring buffer time continuity)
    // Whereas explicitly skipping periods in processing thread skips the least recent frames and thus only misses the intermediate update
    buffer<int32> raw {2*2*N}; // Ring buffer storing unfiltered stereo samples to be recorded
    buffer<float> signal {2*N}; // Ring buffer
    uint writeIndex = 0, readIndex = 0;
    Semaphore readCount {0}; // Audio thread releases input samples, processing thread acquires
    Semaphore writeCount {(int64)signal.size}; // Processing thread releases processed samples, audio thread acquires
    uint periods=0, frames=0, skipped=0; Time lastReport; // For average overlap statistics report

    // Filter
    Notch notch1 {1*50./rate, 1./12}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./12}; // Cascaded to remove the first odd harmonic (3rd partial)

    // Analysis
    PitchEstimator pitchEstimator {N};

    // Result
    uint worstKey = 0;

    // UI
    HarmonicPlot harmonic;
    const uint textSize = 64;
    Text key {""_, textSize, white};
    Text pitch {""_, textSize, white};
    Text fOffset {""_, textSize, white};
    Text fError {""_, textSize, white};
    HBox status {{&key,&pitch,&fOffset,&fError}};
    OffsetPlot profile;
    VBox layout {{&harmonic, &status, &profile}};
    Window window{&layout, int2(1024,600), "Tuner"};

    Tuner() {
        log(__TIME__, input.sampleBits, input.rate, input.periodSize);
        map<string,string> args;
        for(string arg: arguments()) { string key=section(arg,'='), value=section(arg,'=',1); if(key) args.insert(key, value); }
        if(args.contains("ratio"_)) { ratioThreshold=toInteger(args.at("ratio"_))/100.; log("Ratio threshold: ", ratioThreshold); }
        if(args.contains("power"_)) { powerThreshold=toInteger(args.at("power"_)); log("Power threshold: ", powerThreshold); }

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
    uint t =0;
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
            real x = (input[i*2+0]+input[i*2+1]) * 0x1p-32f;
            x = notch1(x);
            x = notch3(x);
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

        float f = pitchEstimator.estimate(frame, fMin);
        uint key = round(pitchToKey(f*rate/N));

        harmonic.rate = rate;
        harmonic.harmonic = pitchEstimator.harmonic;
        harmonic.fMin  = keyToPitch(key-0.5);
        harmonic.fMax = keyToPitch(key+0.5);

        if( pitchEstimator.harmonicMax > powerThreshold /*Harmonic content*/
                && pitchEstimator.harmonicMax / pitchEstimator.harmonicPower > ratioThreshold /*Single pitch*/ ) {

            float expectedF = keyToPitch(key)*N/rate;
            const float offset =  12*log2(f/expectedF);
            const float error =  12*log2((expectedF+1)/expectedF);

            this->key.setText(strKey(key));
            this->pitch.setText(dec(round(f*rate/N )));
            this->fOffset.setText(dec(round(100*offset)));
            this->fError.setText(dec(round(100*error)));

            if(key>=21 && key<21+keyCount) {
                float& keyOffset = profile.offsets[key-21];
                {const float alpha = 1./8; keyOffset = (1-alpha)*keyOffset + alpha*offset;} // Smoothes offset changes (~1sec)
                float variance = sq(offset - keyOffset);
                float& keyVariance = profile.variances[key-21];
                {const float alpha = 1./8; keyVariance = (1-alpha)*keyVariance + alpha*variance;} // Smoothes deviation changes
                {uint k = this->worstKey;
                    for(uint i: range(8,keyCount-8)) //FIXME: quadratic, cubic, exp curve ?
                        if(  min(abs(profile.offsets[i ]), abs(profile.offsets[i ] - 1.f/8 * (float)(i -keyCount/2)/(keyCount/2))) + sqrt(profile.variances[i ]) >
                             min(abs(profile.offsets[k]), abs(profile.offsets[k] - 1.f/8 * (float)(k-keyCount/2)/(keyCount/2))) + sqrt(profile.variances[k]) ) k = i;
                    if(k != this->worstKey) { this->worstKey=k; window.setTitle(strKey(21+k)); }
                }
            }
        }

        readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
        writeCount.release(periodSize); // Releases free samples (only after having possibly recorded the whole ring buffer)
        window.render();
    }
} app;


