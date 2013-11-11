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

// Maps frequency (Hz) to position on X axis (log scale)
float x(float f, int key, int2 size) {
    float min = log2(keyToPitch(key-1./2)), max = log2(keyToPitch(key+1./2));
    return (log2(f)-min)/(max-min) * size.x;
}

struct EstimationPlot : Widget {
    uint key = 0;
    struct Estimation { float fMin, f0, fMax; float confidence; vec3 color; };
    array<Estimation> estimations;

    int2 sizeHint() { return int2(-1024/3, -236); }
    void render(int2 position, int2 size) {
        if(!key) return;
        float maxConfidence = 0; for(Estimation e: estimations) maxConfidence=max(maxConfidence, e.confidence);
        buffer<vec3> target (size.x); clear(target.begin(), size.x, vec3(0));
        for(uint i: range(estimations.size)) { // Fills with an asymetric tent gradient (linear approximation of confidence in log space)
            Estimation e = estimations[i];
            float xMin = x(e.fMin, key, size), x0=x(e.f0, key, size), xMax = x(e.fMax, key, size);
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
            byte4* target = &framebuffer(position.x+x,position.y);
            uint stride = framebuffer.stride;
            //TODO: take inharmonicity into account to widen reference (physical explicit (Railsback) and/or statistical implicit (entropy))
            if(x==(uint)size.x/2) value = 0xFF;
            for(uint y=0; y<size.y*stride; y+=stride) target[y] = value; // Copies along the whole columns
        }
    }
};

//FIXME: factorize with FrequencyPlot?
struct PeriodPlot : Widget {
    uint key = 0;
    ref<float> data;
    const uint rate;

    PeriodPlot(uint rate) : rate(rate) {}
    int2 sizeHint() { return int2(-1024/3, -236); }
    void render(int2 position, int2 size) {
        if(!key) return;
        float y0 = position.y+size.y;
        float fMin = keyToPitch(key-1./2), fMax = keyToPitch(key+1./2);
        int kMin = rate/fMax, kMax = ceil(rate/fMin);
        kMin = max(kMin, 1);
        kMax = min<uint>(kMax, data.size);
        float sMax = 0;
        for(uint k: range(kMin, kMax)) sMax = max(sMax, data[k]);
        if(!sMax) return;
        for(uint k: range(kMin, kMax)) {
            float f0 = (float)rate/(k+1), f1 = (float)rate/k;
            float x0 = x(f0, key, size), x1 = x(f1, key, size);
            float y = data[k] / sMax * size.y;
            fill(position.x+x0,y0-y,position.x+x1,y0,white);
        }
    }
};

//FIXME: factorize with PeriodPlot?
struct FrequencyPlot : Widget {
    uint key = 0;
    ref<float> data; //N/2
    const uint rate;

    FrequencyPlot(uint rate) : rate(rate) {}
    int2 sizeHint() { return int2(-1024/3, -236); }
    void render(int2 position, int2 size) {
        if(!key) return;
        float y0 = position.y+size.y;
        const uint N = data.size*2;
        float fMin = keyToPitch(key-1), fMax = keyToPitch(key+1);
        uint iMin = fMin*N/rate, iMax = ceil(fMax*N/rate);
        iMax = min(iMax, N/2);
        float sMax = 0;
        for(uint i: range(iMin, iMax)) sMax = max(sMax, data[i]);
        if(!sMax) return;
        for(uint i: range(iMin, iMax)) {
            float f0 = (float)i*rate/N, f1 = (float)(i+1)*rate/N;
            float x0 = x(f0, key, size), x1 = x(f1, key, size);
            float y = data[i] / sMax * size.y;
            fill(position.x+x0,y0-y,position.x+x1,y0,white);
        }
    }
};

const uint keyCount = 85;

struct OffsetPlot : Widget {
    float offsets[keyCount] = {};
    float variances[keyCount] = {};
    OffsetPlot() {
      if(!existsFile("offsets.profile"_,config())) return;
      TextData s = readFile("offsets.profile"_,config());
      for(uint i: range(keyCount)) { offsets[i] = s.decimal()/100; s.skip(" "_); variances[i] = sq(s.decimal()/100); s.skip("\n"_); }
    }
    ~OffsetPlot() {
        String s;
        for(uint i: range(keyCount)) s << str(offsets[i]*100) << " "_ << str(sqrt(variances[i])*100) << "\n"_;
        writeFile("offsets.profile"_, s, config());
    }
    int2 sizeHint() { return int2(keyCount*12, -236); }
    void render(int2 position, int2 size) {
        float minimumOffset = -1./8;
        float maximumOffset = 1./8;
        for(int key: range(keyCount)) {
            int x0 = position.x + key * size.x / keyCount;
            int x1 = position.x + (key+1) * size.x / keyCount;

            // TODO: Offset reference according to inharmonicty (~railsback curve)
            float p0 = 0;
            int y0 = position.y + size.y * (maximumOffset-p0) / (maximumOffset-minimumOffset);

            float offset = offsets[key];
            float deviation = sqrt(variances[key]);
            float sign = ::sign(offset-p0) ? : 1;

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
    float noiseFloor = exp2(-8); // Power relative to full scale
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
    //Notch notch3 {3*50./rate, 1./12}; // Cascaded to remove the first odd harmonic (3rd partial)

    PitchEstimator pitchEstimator {N};

    float previousPowers[3] = {0,0,0}; // Power history to trigger estimation only on decay
    array<int> keyEstimations;
    uint instantKey = 0, currentKey = 0, worstKey = 0;


    map<string,string> args;
    PeriodPlot autocorrelation {rate};
    EstimationPlot estimations;
    FrequencyPlot spectrum {rate};
    const uint textSize = 64;
    Text kOffset {""_, textSize, white};
    Text kError {""_, textSize, white};
    Text key {""_, textSize, white};
    Text pitch {""_, textSize, white};
    Text fOffset {""_, textSize, white};
    Text fError {""_, textSize, white};
    OffsetPlot profile;
    /*HList<VBox> grid {
        move(array<VBox>()
                << VBox({&autocorrelation, &kOffset, &kError})
                << VBox({&estimations,  &key, &fOffset})
                << VBox({&spectrum, &pitch, &fError })
             )};
    VBox layout {{&grid, &profile}};*/
    WidgetGrid grid {{&kOffset,&key,&fOffset,&kError,&pitch,&fError}};
    VBox layout {{&estimations, &grid, &profile}};
    Window window{&layout, int2(1024,600), "Tuner"};
    Tuner() {
        log(__TIME__, input.sampleBits, input.rate, input.periodSize);
        for(string arg: arguments()) { string key=section(arg,'='), value=section(arg,'=',1); if(key) args.insert(key, value); }
        if(args.contains("floor"_)) { noiseFloor=exp2(-toInteger(args.at("floor"_))); log("Noise floor: ",log2(noiseFloor),"dB2FS"); }
        if(args.contains("min"_)) { kMax = rate/toInteger(args.at("min"_)); log("Maximum period"_, kMax, "~"_, rate/kMax, "Hz"_); }
        if(args.contains("max"_)) { fMax = toInteger(args.at("max"_))*N/rate; log("Maximum frequency"_, fMax, "~"_, fMax*rate/N, "Hz"_); }
        window.backgroundColor=window.backgroundCenter=0;
        window.localShortcut(Escape).connect([]{exit();}); //FIXME: threads waiting on semaphores will be stuck
        window.show();
#if TEST
        timer.timeout.connect(this, &Tuner::feed);
        timer.setRelative(1);
        assert_(audio.rate == input.rate, audio.rate, input.rate);
        noiseFloor = exp2(-16);
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
            //FIXME: the notches might also affects nearby keys
            /*if(abs(instantKey-pitchToKey(notch1.frequency*rate)) > 1 || previousPowers[0]<2*noiseFloor)*/ x = notch1(x);
            //if(abs(instantKey-pitchToKey(notch3.frequency*rate)) > 1 || previousPowers[0]<2*noiseFloor) x = notch3(x);
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

            this->kOffset.setText(dec(round(100*kOffset)));
            this->kError.setText(dec(round(100*kError)));
            this->key.setText(strKey(key));
            this->pitch.setText(dec(round(fError<kError ? f*rate/N : rate/k)));
            this->fOffset.setText(dec(round(100*fOffset)));
            this->fError.setText(dec(round(100*fError)));

            if(/*power > 2*noiseFloor &&*/ key>=21 && key<21+keyCount && key==currentKey && maxCount>=2) {
                float offset = kError<fError ? kOffset : fOffset;
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
        } else if(keyEstimations.size) keyEstimations.take(0);

        readIndex = (readIndex+periodSize)%signal.size; // Updates ring buffer pointer
        writeCount.release(periodSize); // Releases free samples (only after having possibly recorded the whole ring buffer)
        shiftPeriods(power);

        // Autocorrelation
        autocorrelation.key = instantKey;
        autocorrelation.data = pitchEstimator.autocorrelations;

        // Spectrum
        spectrum.key = instantKey;
        spectrum.data = pitchEstimator.spectrum;

        // FIXME: -> EstimationPlot
        // Always add estimations so that they always fade out at the same speed
        while(estimations.estimations.size > 16) estimations.estimations.take(0); //FIXME: ring buffer
        estimations.estimations << EstimationPlot::Estimation{rate/(k+1), rate/k, rate/(k-1), power, vec3(0,kError<fError,1)};
        estimations.estimations << EstimationPlot::Estimation{(f-1)*rate/N, f*rate/N, (f+1)*rate/N, power, vec3(1,fError<kError,0)};
        estimations.key = instantKey;
        window.render();
    }
    void shiftPeriods(float power) {
        previousPowers[2] = previousPowers[1];
        previousPowers[1] = previousPowers[0];
        previousPowers[0] = power;
    }
} app;


