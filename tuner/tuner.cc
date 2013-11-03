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
#if __x86_64
#define TEST 1
#endif

/// Estimates fundamental frequency (~pitch) of audio input
struct Tuner : Widget, Poll {
    // Static parameters
    static constexpr uint N = 16384; // Analysis window size (A-1 (27Hz~2K))
    const uint periodSize = 4096; // Overlaps to increase time resolution (compensates loss from Hann window (which improves frequency resolution))

    // Input
    Thread thread; // Audio thread to buffer periods (when kernel driver buffer was configured too low)
    AudioInput input{{this,&Tuner::write}, 96000, periodSize, thread};
    const uint rate = input.rate;

#if TEST
    buffer<float2> stereo = decodeAudio(Map("Samples/3.flac"_)); //FIXME: record 96000KHz sample
    Timer timer {thread};
    Time realTime;
    Time totalTime;
    float frameTime = 0;
#endif

    // Input-dependent parameters
    float noiseThreshold = exp2(-8); // Power relative to full scale
    uint fMin = N*440*exp2(-4 - 0./12 - (1./2 / 12))/rate; // ~27 Hz ~ half semitone under A-1
    uint fMax = N*440*exp2(3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half semitone over C7

    // A large buffer is preferred as overflows would miss most recent frames (and break the ring buffer time continuity)
    // Whereas explicitly skipping periods in processing thread skips the least recent frames and thus only misses the intermediate update
    buffer<int32> raw {2*2*N}; // Ring buffer storing unfiltered stereo samples to be recorded
    buffer<float> signal {2*N}; // Ring buffer
    uint writeIndex = 0, readIndex = 0;
    Semaphore readCount {0}; // Audio thread releases input samples, processing thread acquires
    Semaphore writeCount {(int64)signal.size}; // Processing thread releases processed samples, audio thread acquires
    uint periods=0, frames=0, skipped=0; Time lastReport; // For average overlap statistics report

    Notch notch1 {1*50./rate, 1./2}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./3}; // Cascaded to remove the first odd harmonic (3rd partial)

    PitchEstimator pitchEstimator {N};

    float previousPowers[3] = {0,0,0}; // Power history to trigger estimation only on decay
    int currentKey = 0;
    struct Estimation { float fMin, f0, fMax; float confidence; vec3 color; };
    array<Estimation> estimations;

    UniformGrid<Text> info {3,2};
    VBox layout {{this, &info}};
    Window window{&layout, int2(1024,600), "Tuner"};

    Tuner() {
        log(input.sampleBits, input.rate, input.periodSize);

        if(arguments().size>0) { noiseThreshold=exp2(toDecimal(arguments()[0])); log("Noise threshold: ",log2(noiseThreshold),"dB2FS"); }
        if(arguments().size>1) { fMin = toInteger(arguments()[1])*N/rate; log("Minimum frequency"_, fMin, "~"_, fMin*rate/N, "Hz"_); }
        if(arguments().size>2) { fMax = toInteger(arguments()[2])*N/rate; log("Maximum frequency"_, fMax, "~"_, fMax*rate/N, "Hz"_); }
        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();}); //FIXME: threads waiting on semaphores will be stuck
        info << Text(str(input.sampleBits)) << Text(str(input.rate)) << Text(str(input.periodSize))
             << Text("correlation"_) << Text("reference"_) << Text("peak"_);
        window.show();
#if TEST
        timer.timeout.connect(this, &Tuner::feed);
        timer.setRelative(1);
#else
        input.start();
#endif
        thread.spawn();
    }
#if TEST
    uint t =0;
    void feed() {
        const uint size = input.periodSize;
        if(t+size > stereo.size) { exit(); return; }
        const float2* period = stereo + t;
        int32 buffer[size*2];
        for(uint i: range(size)) {
            buffer[i*2+0] = period[i][0] * 0x1p8f; // 24->32
            buffer[i*2+1] = period[i][1] * 0x1p8f; // 24->32
        }
        t += size;
        write(buffer, size);
        float time = ((float)size/rate) / (float)realTime; // Instant playback rate
        const float alpha = 1./16; frameTime = (1-alpha)*frameTime + alpha*(float)time; // IIR Smoother
        //if((t/size)%(rate/size)==0) log(dec( ((float)t/rate) / (float)totalTime));
        realTime.reset();
        timer.setRelative(size*1000/rate/8); // 8xRT
    }
#endif
    uint write(const int32* output, uint size) {
        writeCount.acquire(size); // Will overflow if processing thread doesn't follow
        assert(writeIndex+size<=signal.size);
        for(uint i: range(size)) {
            raw[2*(writeIndex+i)+0] = output[i*2+0];
            raw[2*(writeIndex+i)+1] = output[i*2+1];
            real x = (output[i*2+0]+output[i*2+1]) * 0x1p-33f;
            //FIXME the notches also affects nearby keys
            if(currentKey != int(round(pitchToKey(notch1.frequency*rate)))) x = notch1(x);
            if(currentKey != int(round(pitchToKey(notch3.frequency*rate)))) x = notch3(x);
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
            shiftPeriods(previousPowers[0]); // Shifts in a dummy period for decay detection purposes
        }
        readCount.acquire(periodSize); periods++;
        frames++;
        buffer<float> frame {N}; // Need a linear buffer for autocorrelation (FIXME: mmap ring buffer)
        assert(readIndex+periodSize<=N);
        for(uint i: range(min<int>(N,signal.size-readIndex))) frame[i] = signal[readIndex+i]; // Copies ring buffer head into linear frame buffer
        for(uint i: range(signal.size-readIndex, N)) frame[i] = signal[i+readIndex-signal.size]; // Copies ring buffer tail into linear frame buffer

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

        if(power > noiseThreshold && previousPowers[1] > previousPowers[0]/2 && previousPowers[0] > power) {
            const uint textSize = 64;
            info[0] = Text(dec(round(100*kOffset)), textSize, white);
            info[3] = Text(dec(round(100*kError)), textSize, white);
            info[1] = Text(strKey(max(0,key)), textSize, white);
            info[4] = Text(dec(round(fError<kError ? f*rate/N : rate/k)), textSize, white);
            info[2] = Text(dec(round(100*fOffset)), textSize, white);
            info[5] = Text(dec(round(100*fError)), textSize, white);
            currentKey = key;
            if(previousPowers[2] < previousPowers[1] && key>=21 && key<21+88) { // t-2 is the attack
                Folder folder("samples"_, home(), true);
                uint velocity = round(0x100*sqrt(power)); // Decay power is most stable (FIXME: automatic velocity normalization)
                uint velocities[88] = {};
                for(string name: folder.list(Files)) {
                    TextData s (name); uint fKey = s.integer(); if(fKey>=88 || !s.match('-')) continue; uint fVelocity = s.hexadecimal();
                    if(fVelocity > velocities[fKey]) velocities[fKey] = fVelocity;
                }
                if(velocity >= velocities[key-21]) { // Records new sample only if higher velocity than existing sample for this key
                    velocities[key-21] = velocity;
                    File record(dec(key-21,2)+"-"_+hex(velocity,2), folder, Flags(WriteOnly|Create|Truncate));
                    record.write(cast<byte>(raw.slice(readIndex*2,raw.size))); // Copies ring buffer head into file
                    record.write(cast<byte>(raw.slice(0,readIndex*2))); // Copies ring buffer tail into file
                }
                log(apply(ref<uint>(velocities),[](uint v){ return "0123456789ABCDF"_[min(0xFFu,v/0x10)]; }));
                log(strKey(max(0,key))+"\t"_ +str(round(rate/k))+" Hz\t"_+dec(log2(power))+"\t"_+dec(velocity));
                log("k\t"_+dec(round(100*kOffset))+" \t+/-"_+dec(round(100*kError))+" cents\t"_);
                log("f\t"_+dec(round(100*fOffset))+" \t+/-"_+dec(round(100*fError))+" cents\t"_);
            }
        }
        readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
        writeCount.release(periodSize); // Releases free samples (only after having possibly recorded the whole ring buffer)
        shiftPeriods(power);

        // Always add estimations so that they always fade out at the same speed
        while(estimations.size > 16) estimations.take(0); //FIXME: ring buffer
        estimations << Estimation{rate/(k+1), rate/k, rate/(k-1), power, vec3(0,kError<fError,1)};
        estimations << Estimation{(f-1)*rate/N, f*rate/N, (f+1)*rate/N, power, vec3(1,fError<kError,0)};
        window.render();
    }
    void shiftPeriods(float power) {
        previousPowers[2] = previousPowers[1];
        previousPowers[1] = previousPowers[0];
        previousPowers[0] = power;
    }
    void render(int2 position, int2 size) {
        auto x = [&](float f)->float { // Maps frequency (Hz) to position on X axis (log scale)
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
            a0 *= (float) (i/2+1) / (estimations.size/2); // Time
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


