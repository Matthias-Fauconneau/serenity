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

const uint keyCount = 85;

struct OffsetPlot : Widget {
    float offsets[keyCount] = {};
    float variances[keyCount] = {};
    int2 sizeHint() { return int2(keyCount*12, 192); }
    void render(int2 position, int2 size) {
        float minimumOffset = -1./4;
        float maximumOffset = 1./4;
        for(int key: range(keyCount)) {
            int x0 = position.x + key * size.x / keyCount;
            int x1 = position.x + (key+1) * size.x / keyCount;
            int y0 = position.y + size.y * maximumOffset / (maximumOffset-minimumOffset);
            float offset = offsets[key];
            float deviation = sqrt(variances[key]);
            float sign = ::sign(offset) ? : 1;

            // High confidence between zero and max(0, |offset|-deviation)
            float p1 = max(0.f, abs(offset)-deviation);
            int y1 = position.y + size.y * (maximumOffset-sign*p1) / (maximumOffset-minimumOffset);
            fill(x0,y0<y1?y0:y1,x1,y0<y1?y1:y0, sign*p1>0 ? vec4(1,0,0,1) : vec4(0,0,1,1));

            // Mid confidence between max(0,|offset|-deviation) and |offset|
            float p2 = abs(offset);
            int y2 = position.y + size.y * (maximumOffset-sign*p2) / (maximumOffset-minimumOffset);
            fill(x0,y1<y2?y1:y2,x1,y1<y2?y2:y1, sign*p2>0 ? vec4(3./4,0,0,1) : vec4(0,0,3./4,1));

            // Low confidence between |offset| and |offset|+deviation
            float p3 = abs(offset)+deviation;
            int y3 = position.y + size.y * (maximumOffset-sign*p3) / (maximumOffset-minimumOffset);
            fill(x0,y2<y3?y2:y3,x1,y2<y3?y3:y2, sign*p3>0 ? vec4(1./2,0,0,1) : vec4(0,0,1./2,1));

            // Low confidence between min(|offset|-deviation, 0) and zero
            float p4 = min(0.f, abs(offset)-deviation);
            int y4 = position.y + size.y * (maximumOffset-sign*p4) / (maximumOffset-minimumOffset);
            fill(x0,y0<y4?y0:y4,x1,y0<y4?y4:y0, sign*p4>0 ? vec4(1./2,0,0,1) : vec4(0,0,1./2,1));
        }
    }
};

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
    Audio audio = decodeAudio("/Samples/A0-C3.flac"_);
    Timer timer {thread};
    Time realTime;
    Time totalTime;
    float frameTime = 0;
#endif

    // Input-dependent parameters
    float noiseFloor = exp2(-15); // Power relative to full scale
    uint kMax = rate / (440*exp2(-4 - 0./12 - (1./2 / 12))); // ~27 Hz ~ half pitch under A-1 (k ~ 3593 samples at 96 kHz)
    uint fMax = N*440*exp2(3 + 3./12 + (1./2 / 12))/rate; // ~4308 Hz ~ half semitone over C7

    // A large buffer is preferred as overflows would miss most recent frames (and break the ring buffer time continuity)
    // Whereas explicitly skipping periods in processing thread skips the least recent frames and thus only misses the intermediate update
    buffer<int32> raw {2*2*N}; // Ring buffer storing unfiltered stereo samples to be recorded
    buffer<float> signal {2*N}; // Ring buffer
    uint writeIndex = 0, readIndex = 0;
    Semaphore readCount {0}; // Audio thread releases input samples, processing thread acquires
    Semaphore writeCount {(int64)signal.size}; // Processing thread releases processed samples, audio thread acquires
    uint periods=0, frames=0, skipped=0; Time lastReport; // For average overlap statistics report

    Notch notch1 {1*50./rate, 1./12}; // Notch filter to remove 50Hz noise
    Notch notch3 {3*50./rate, 1./12}; // Cascaded to remove the first odd harmonic (3rd partial)

    PitchEstimator pitchEstimator {N};

    float previousPowers[3] = {0,0,0}; // Power history to trigger estimation only on decay
    array<int> keyEstimations;
    uint instantKey = 0, currentKey = 0;
    struct Estimation { float fMin, f0, fMax; float confidence; vec3 color; };
    array<Estimation> estimations;

    map<string,string> args;
    UniformGrid<Text> info {3,2};
    OffsetPlot offsets;
    VBox layout {{this, &info, &offsets}};
    Window window{&layout, int2(1024,600), "Tuner"};
    Tuner() {
        log(__TIME__);
        log(input.sampleBits, input.rate, input.periodSize,
            strKey(round(pitchToKey(notch1.frequency*rate))), strKey(round(pitchToKey(notch3.frequency*rate))));
        for(string arg: arguments()) { string key=section(arg,'='), value=section(arg,'=',1); if(key) args.insert(key, value); }
        if(args.contains("floor"_)) { noiseFloor=exp2(toDecimal(args.at("floor"_))); log("Noise floor: ",log2(noiseFloor),"dB2FS"); }
        if(args.contains("min"_)) { kMax = rate/toInteger(args.at("min"_)); log("Maximum period"_, kMax, "~"_, rate/kMax, "Hz"_); }
        if(args.contains("max"_)) { fMax = toInteger(args.at("max"_))*N/rate; log("Maximum frequency"_, fMax, "~"_, fMax*rate/N, "Hz"_); }
        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();}); //FIXME: threads waiting on semaphores will be stuck
        info.grow(6);
        window.show();
#if TEST
        timer.timeout.connect(this, &Tuner::feed);
        timer.setRelative(1);
        assert_(audio.rate == input.rate, audio.rate, input.rate);
#else
        input.start();
#endif
        //thread.spawn(); //DEBUG
        offsets.offsets[keyCount/2-3]=-1./8;
        offsets.variances[keyCount/2-3]=1./256;
        offsets.offsets[keyCount/2-2]=-1./16;
        offsets.variances[keyCount/2-2]=1./256;
        offsets.offsets[keyCount/2-1]=-1./32;
        offsets.variances[keyCount/2-1]=1./256;
        offsets.offsets[keyCount/2]=0;
        offsets.variances[keyCount/2]=1./256;
        offsets.offsets[keyCount/2+3]=+1./8;
        offsets.variances[keyCount/2+3]=1./256;
        offsets.offsets[keyCount/2+2]=+1./16;
        offsets.variances[keyCount/2+2]=1./256;
        offsets.offsets[keyCount/2+1]=+1./32;
        offsets.variances[keyCount/2+1]=1./256;
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
        //if((t/size)%(rate/size)==0) log(dec( ((float)t/rate) / (float)totalTime));
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
            //FIXME: the notches might also affects nearby keys
            if(abs(instantKey-pitchToKey(notch1.frequency*rate)) > 1 || previousPowers[0]<exp2(-15)) x = notch1(x);
            if(abs(instantKey-pitchToKey(notch3.frequency*rate)) > 1 || previousPowers[0]>exp2(-15)) x = notch3(x);
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
            //shiftPeriods(previousPowers[0]); // Shifts in a dummy period for decay detection purposes
        }
        readCount.acquire(periodSize); periods++;
        frames++;
        buffer<float> frame {N}; // Need a linear buffer for autocorrelation (FIXME: mmap ring buffer)
        assert(readIndex+periodSize<=signal.size);
        for(uint i: range(min<int>(N,signal.size-readIndex))) frame[i] = signal[readIndex+i]; // Copies ring buffer head into linear frame buffer
        for(uint i: range(signal.size-readIndex, N)) frame[i] = signal[i+readIndex-signal.size]; // Copies ring buffer tail into linear frame buffer

        float k = pitchEstimator.estimate(frame, kMax, fMax);
        float power = pitchEstimator.power;
        uint key = round(pitchToKey(rate/k));

        float expectedK = rate/keyToPitch(key);
        const float kOffset = 12*log2(expectedK/k);
        const float kError = 12*log2((expectedK+1)/expectedK);

        float expectedF = keyToPitch(key)*N/rate;
        float f = pitchEstimator.period ? pitchEstimator.fPeak / pitchEstimator.period : 0;
        const float fOffset =  12*log2(f/expectedF);
        const float fError =  12*log2((expectedF+1)/expectedF);

        if(power > noiseFloor && previousPowers[0] > power/16) {
            instantKey = key;
            if(keyEstimations.size>=3) keyEstimations.take(0);
            keyEstimations << key;
            map<int,int> count; int maxCount=0;
            for(int key: keyEstimations) { count[key]++; maxCount = max(maxCount, count[key]); }
            array<int> maxKeys; for(int key: keyEstimations) if(count[key]==maxCount) maxKeys << key; // Keeps most frequent keys
            currentKey = maxKeys.last(); // Resolve ties by taking last (most recent)

            const uint textSize = 64;
            info[0] = Text(dec(round(100*kOffset)), textSize, white);
            info[3] = Text(dec(round(100*kError)), textSize, white);
            info[1] = Text(strKey(key), textSize, white);
            info[4] = Text(dec(round(fError<kError ? f*rate/N : rate/k)), textSize, white);
            info[2] = Text(dec(round(100*fOffset)), textSize, white);
            info[5] = Text(dec(round(100*fError)), textSize, white);
            if(args.contains("gate"_)) { // DEBUG: Shows notch states
                info.grow(8);
                info[6] = Text(dec(notch1.frequency*rate)+": "_+(abs(currentKey-pitchToKey(notch1.frequency*rate)) > 1?"ON"_:"OFF"_), textSize, white);
                info[7] = Text(dec(notch3.frequency*rate)+": "_+(abs(currentKey-pitchToKey(notch3.frequency*rate)) > 1?"ON"_:"OFF"_), textSize, white);
            }

            if(key>=21 && key<21+keyCount && key==currentKey && maxCount>=2) {
                float offset = kError<fError ? kOffset : fOffset;
                float& keyOffset = offsets.offsets[key-21];
                {const float alpha = 1./8; keyOffset = (1-alpha)*keyOffset + alpha*offset;} // Smoothes offset changes (~1sec)
                float variance = sq(offset - keyOffset);
                float& keyVariance = offsets.variances[key-21];
                {const float alpha = 1./8; keyVariance = (1-alpha)*keyVariance + alpha*variance;} // Smoothes deviation changes
            }

#if 0
            if(previousPowers[1] > previousPowers[0]/2 && previousPowers[0] > power &&
                    previousPowers[2] < previousPowers[1] && key>=21 && key<21+keyCount) { // t-2 is the attack
                Folder folder("samples"_, home(), true);
                uint velocity = round(0x100*sqrt(power)); // Decay power is most stable (FIXME: automatic velocity normalization)
                uint velocities[keyCount] = {};
                for(string name: folder.list(Files)) {
                    TextData s (name); uint fKey = s.integer(); if(fKey>=keyCount || !s.match('-')) continue; uint fVelocity = s.hexadecimal();
                    if(fVelocity > velocities[fKey]) velocities[fKey] = fVelocity;
                }
                if(velocity >= velocities[key-21]) { // Records new sample only if higher velocity than existing sample for this key
                    velocities[key-21] = velocity;
                    File record(dec(key-21,2)+"-"_+hex(velocity,2), folder, Flags(WriteOnly|Create|Truncate));
                    record.write(cast<byte>(raw.slice(readIndex*2,raw.size))); // Copies ring buffer head into file
                    record.write(cast<byte>(raw.slice(0,readIndex*2))); // Copies ring buffer tail into file
                }
                log(apply(ref<uint>(velocities),[](uint v){ return "0123456789ABCDF"_[min(0xFFu,v/0x10)]; }));
                log(strKey(key)+"\t"_ +str(round(rate/k))+" Hz\t"_+dec(log2(power))+"\t"_+dec(velocity));
                log("k\t"_+dec(round(100*kOffset))+" \t+/-"_+dec(round(100*kError))+" cents\t"_);
                log("f\t"_+dec(round(100*fOffset))+" \t+/-"_+dec(round(100*fError))+" cents\t"_);
            }
#endif
        } else if(keyEstimations.size) keyEstimations.take(0);

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
            float min = log2(keyToPitch(instantKey-1)), max = log2(keyToPitch(instantKey+1));
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


