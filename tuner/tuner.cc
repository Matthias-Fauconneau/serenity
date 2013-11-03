#include "thread.h"
#include "math.h"
#include "sampler.h"
#include "time.h"
#include "pitch.h"
#include "sequencer.h"
#include "audio.h"
#include "display.h"
#include "text.h"
#include "layout.h"
#include "window.h"
#define TEST 1
//TODO: Profile and optimize to run without overflow on Atom

/// Estimates fundamental frequency (~pitch) of audio input
struct Tuner : Widget, Poll {
    static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
    PitchEstimator pitchEstimator {N};
    Thread thread {-20}; // Audio thread to buffer periods (when kernel driver buffer was configured too low)
    AudioInput input{{this,&Tuner::write16}, {this,&Tuner::write32}, -1, N, thread};
    const uint rate = input.rate;

#if TEST
    buffer<float2> stereo = decodeAudio(Map("Samples/3.flac"_));
    Timer timer {thread};
    Time realTime;
    Time totalTime;
    float frameTime = 0;
#endif

    float noiseThreshold = exp2(-8); // Power relative to full scale
    uint fMin = N*440*exp2(-4 - 0./12 - (1./2 / 12))/rate; // ~27 Hz ~ half semitone under A-1
    uint fMax = N*440*exp2(3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half semitone over C7

    float previousPowers[2] = {0,0}; // Power history to trigger estimation only on decay
    buffer<float> signal {N}; // Ring buffer (may be larger to allow some lag instead of overflow)
    uint writeIndex = 0, readIndex = 0;
    Semaphore readCount {0}; // Audio thread releases input samples, processing thread acquires
    Semaphore writeCount {(int64)signal.size}; // Processing thread releases processed samples, audio thread acquires
    Notch notch0 {50./rate, 1}; // Notch filter to remove 50Hz noise
    Notch notch1 {150./rate, 1}; // Cascaded to remove the first odd harmonic (3rd partial)

    int currentKey = 0;
    struct Estimation { float fMin, f0, fMax; float confidence; vec3 color; };
    array<Estimation> estimations;

    UniformGrid<Text> info {3,2};
    VBox layout {{this, &info}};
    Window window{&layout, int2(1024,600), "Tuner"};

    Tuner() {
        log(input.sampleBits, input.rate, input.periodSize);
        info << Text(str(input.sampleBits)) << Text(str(input.rate)) << Text(str(input.periodSize))
             << Text("correlation"_) << Text("reference"_) << Text("peak"_);
        if(arguments().size>0) { noiseThreshold=exp2(toDecimal(arguments()[0])); log("Noise threshold: ",log2(noiseThreshold),"dB2FS"); }
        if(arguments().size>1) { fMin = toInteger(arguments()[1])*N/rate; log("Minimum frequency"_, fMin, "~"_, fMin*rate/N, "Hz"_); }
        if(arguments().size>2) { fMax = toInteger(arguments()[2])*N/rate; log("Maximum frequency"_, fMax, "~"_, fMax*rate/N, "Hz"_); }
        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();});
        window.show();
#if TEST
        timer.timeout.connect(this, &Tuner::feed);
        timer.setRelative(1);
        thread.spawn();
#else
        input.start();
#endif
    }
#if TEST
    uint t =0;
    void feed() {
        log("feed");
        const uint size = 4096;
        if(t+size > stereo.size) { exit(); return; }
        const float2* period = stereo + t;
        int16 buffer[size*2];
        for(uint i: range(size)) {
            buffer[i*2+0] = period[i][0] * 0x1p-8f; // 24->16
            buffer[i*2+1] = period[i][1] * 0x1p-8f; // 24->16
        }
        t += size;
        write16(buffer, size);
        //timer.setRelative(size*1000/rate);
        float time = ((float)size/rate) / (float)realTime; // Instant playback rate
        const float alpha = 1./16; frameTime = (1-alpha)*frameTime + alpha*(float)time; // IIR Smoother
        //if(isPowerOfTwo(t) && t>1) log(dec( ((float)t/rate) / (float)totalTime));
        realTime.reset();
        timer.setRelative(1);
    }
#endif
    uint write16(const int16* output, uint size) {
        writeCount.acquire(size); // Will overflow if processing thread doesn't follow
        assert(writeIndex+size<=signal.size);
        for(uint i: range(size)) signal[writeIndex+i] = notch0(notch1( (int(output[i*2+0])+int(output[i*2+1])) * 0x1p-17f ));
        writeIndex = (writeIndex+size)%signal.size; // Updates ring buffer pointer
        readCount.release(size); // Releases new samples
        queue(); // Queues processing thread
        return size;
    }
    uint write32(const int32* output, uint size) {
        log("write ?",writeCount);
        writeCount.acquire(size); // Will overflow if processing thread doesn't follow
        log("write ok",writeCount);
        assert(writeIndex+size<=signal.size);
        for(uint i: range(size)) signal[writeIndex+i] = notch0(notch1( (int(output[i*2+0])+int(output[i*2+1])) * 0x1p-33f ));
        writeIndex = (writeIndex+size)%signal.size; // Updates ring buffer pointer
        log("readCount was", readCount);
        readCount.release(size); // Releases new samples
        log("readCount <-", readCount);
        queue(); // Queues processing thread
        return size;
    }
    void event() {
        const uint overlap = 1; // Limits to 0% overlap
        const uint size = N/overlap;
        log("readCount ?", readCount);
        readCount.acquire(size);
        log("readCount OK", readCount);
        buffer<float> frame {N}; // Need a linear buffer for autocorrelation (FIXME: mmap ring buffer)
        assert(readIndex+size<=N);
        for(uint i: range(signal.size-readIndex)) frame[i] = signal[readIndex+i]; // Copies head into ring buffer
        for(uint i: range(readIndex)) frame[signal.size-readIndex+i] = signal[i]; // Copies tail into ring buffer
        readIndex = (readIndex+size)%signal.size; // Updates ring buffer pointer
        log("writeCount was", writeCount);
        writeCount.release(size); // Releases free samples
        log("writeCount <-", writeCount);

        float k = pitchEstimator.estimate(frame, fMin, fMax);
        float power = pitchEstimator.power;
        int key = round(pitchToKey(rate/k));

        float expectedK = rate/keyToPitch(key);
        const float kOffset = 12*log2(expectedK/k);
        const float kError = 12*log2((expectedK+1)/expectedK);

        float expectedF = keyToPitch(key)*N/rate;
        float f = pitchEstimator.period ? pitchEstimator.fPeak / pitchEstimator.period : 0;
        const float fOffset =  12*log2(f/expectedF);
        const float fError =  12*log2((expectedF+1)/expectedF);

        if(power > noiseThreshold && previousPowers[1] > power && previousPowers[0] > previousPowers[1]/2) {
            /*log(strKey(max(0,key))+"\t"_ +str(round(rate/k))+" Hz\t"_+dec(log2(power)));
            log("k\t"_+dec(round(100*kOffset))+" \t+/-"_+dec(round(100*kError))+" cents\t"_);
            log("f\t"_+dec(round(100*fOffset))+" \t+/-"_+dec(round(100*fError))+" cents\t"_);*/
            const uint textSize = 64;
            info[0] = Text(dec(round(100*kOffset)), textSize, white);
            info[3] = Text(dec(round(100*kError)), textSize, white);
            info[1] = Text(strKey(max(0,key)), textSize, white);
            info[4] = Text(dec(round(fError<kError ? f*rate/N : rate/k)), textSize, white);
            info[2] = Text(dec(round(100*fOffset)), textSize, white);
            info[5] = Text(dec(round(100*fError)), textSize, white);
            //if(currentKey != key) estimations.clear();
            currentKey = key;
        }

        //if(estimations.size > 2) estimations.take(0); // Prevents clutter from outdated estimations when tuning a single key
        /*if(key == currentKey) { // Only shows estimation for the current key
            if(kError<fError*2) estimations << Estimation{rate/(k+1), rate/k, rate/(k-1), power, vec3(0,kError<fError,1)};
            if(fError<kError*2) estimations << Estimation{(f-1)*rate/N, f*rate/N, (f+1)*rate/N, power, vec3(1,fError<kError,0)};
            window.render();
        }*/
        // Always add estimations so that they always fade out at the same speed
        while(estimations.size > 16) estimations.take(0);
        /*if(kError<fError*2)*/ estimations << Estimation{rate/(k+1), rate/k, rate/(k-1), power, vec3(0,kError<fError,1)};
        /*if(fError<kError*2)*/ estimations << Estimation{(f-1)*rate/N, f*rate/N, (f+1)*rate/N, power, vec3(1,fError<kError,0)};
        window.render();

        previousPowers[0] = previousPowers[1];
        previousPowers[1] = power;
    }
    void render(int2 position, int2 size) {
        auto x = [&](float f)->float{ // Maps frequency (Hz) to position on X axis (log scale)
            float min = log2(keyToPitch(currentKey-1)), max = log2(keyToPitch(currentKey+1));
            return (log2(f)-min)/(max-min) * size.x;
        };

        float maxConfidence = 0; for(Estimation e: estimations) maxConfidence=max(maxConfidence, e.confidence);
        buffer<vec3> target (size.x); clear(target.begin(), size.x, vec3(0));
        for(uint i: range(estimations.size)) { // Fills with an asymetric tent gradient (linear approximation of confidence in log space)
            Estimation e = estimations[i];
            float xMin = x(e.fMin), x0=x(e.f0), xMax = x(e.fMax);
            float a0 = 1;
            a0 *= e.confidence / maxConfidence; // Power
            a0 *= (float) (i+1) / estimations.size; // Time (also makes k always slightly more transparent)
            for(uint x: range(max<int>(0,xMin+1), min<int>(size.x,x0+1))) {
                float a = a0 * (1-(x0-x)/(x0-xMin));
                assert(a>=0 && a<=1, a, xMin, x, x0);
                target[x] = (1-a)*target[x] + a*e.color; // Blend
            }
            for(uint x: range(max<int>(0,x0+1), min<int>(size.x,xMax))) {
                float a = a0 * (1-(x-x0)/(xMax-x0));
                assert(a>=0 && a<=1, a, x0, x, xMax);
                target[x] = (1-a)*target[x] + a*e.color; // Blend
            }
        }
        for(uint x: range(size.x)) {
            byte4 value (sRGB(target[x].x), sRGB(target[x].y), sRGB(target[x].z), 0xFF);  // Converts to sRGB
            byte4* target = &framebuffer(x,position.y);
            uint stride = framebuffer.stride;
            //TODO: take inharmonicity into account to widen reference (physical explicit (Railsback) and/or statistical implicit (entropy))
            if(x==(uint)size.x/2) value = 0xFF;
            for(uint y=0; y<size.y*stride; y+=stride) target[y] = value; // Copies along the whole columns
        }
    }
} app;


