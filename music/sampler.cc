#include "simd.h"
#include "sampler.h"
#include "resample.h"
#include "data.h"
#include "file.h"
#include "flac.h"
#include "time.h"
#include "string.h"
#include "math.h"

static Sampler* sampler; // DEBUG

struct Audio : buffer<float2> { uint rate; Audio(buffer&& data, uint rate) : buffer(::move(data)), rate(rate){} };
/// Decodes a full audio file
Audio decodeAudio(const ref<byte>& data, uint duration=-1);

Audio decodeAudio(const ref<byte>& data, uint duration) {
    FLAC flac(data);
    duration = ::min(duration, flac.duration);
    flac.audio = buffer<float2>(max(32768u,duration+8192));
    while(flac.audioAvailable<duration) flac.decodeFrame();
    return {copyRef(flac.audio.slice(0, duration)), flac.rate};
}

struct Sampler::Sample {
    String name;
    Map data; FLAC flac; array<float> envelope; //Sample data
    int trigger=0; uint lovel=0; uint hivel=127; uint lokey=0; uint hikey=127; uint locc64=0, hicc64=127; uint random=0; //Input controls
    int pitch_keycenter=60; float releaseTime=1; float amp_veltrack=1; float volume=1; //Performance parameters
    int rt_decay=0;
    float startLevel; // Sound level of first 2K samples
    uint decayTime; // Time (in samples) where level decays below
};
inline String str(const Sampler::Sample& s) { return str(s.lokey)+"-"_+str(s.pitch_keycenter)+"-"_+str(s.hikey); }
inline bool operator <(const  Sampler::Sample& a, const Sampler::Sample& b) { return a.pitch_keycenter<b.pitch_keycenter; }

struct Note {
    default_move(Note);
    explicit Note(FLAC&& flac) : flac(move(flac)), readCount(this->flac.audioAvailable), writeCount(this->flac.audio.capacity-this->flac.audioAvailable) {}
    FLAC flac;
    v4sf level; // Current note attenuation
    v4sf step; // Coefficient for release fade out = (2 ** -16)**(1/releaseTime)
    Semaphore readCount; // Decoder thread releases decoded samples, audio thread acquires
    Semaphore writeCount; // Audio thread release free samples, decoder thread acquires
    //Lock lock; // Protects \a step
    float releaseTime; // To compute step
    uint key=0, velocity=0; // To match release sample
    ref<float> envelope; // To level release sample
    uint decayTime = 0;
    /// Decodes frames until \a available samples is over \a need
    void decode(uint need);
    /// Reads samples to be mixed into \a output
    void read(mref<float2> output);
    /// Computes sum of squares on the next \a size samples (to compute envelope)
    float sumOfSquares(uint size);
    /// Computes actual sound level on the next \a size samples (using precomputed envelope)
    float actualLevel(uint size) const;
};
inline String str(const Note& o) {
    return str(o.flac.position, o.decayTime, o.flac.duration);
}

struct Sampler::Layer {
    float shift;
    array<Note> notes; // Active notes (currently being sampled) in this layer
    Resampler resampler; // Resampler to shift pitch
    buffer<float2> audio; // Buffer to mix notes before resampling
};

int noteToMIDI(const string& value) {
    int note=24;
    uint i=0;
    assert(value[i]>='a' && value[i]<='g');
    note += "c#d#ef#g#a#b"_.indexOf(value[i]);
    i++;
    if(value.size==3) {
        if(value[i]=='#') { note++; i++; }
        else error(value);
    }
    assert(value[i]>='0' && value[i]<='8', value);
    note += 12*(value[i]-'0');
    return note;
}

template<int unroll> inline void accumulate(v4sf accumulators[unroll], const v4sf* ptr, const v4sf* end) {
    for(;ptr<end;ptr+=unroll) {
        for(uint i: range(unroll)) accumulators[i] += ptr[i]*ptr[i];
    }
}
double sumOfSquares(const FLAC& flac, uint size) {
    size =size/2; uint index = flac.readIndex/2, capacity = flac.audio.capacity/2; // floor to v4sf
    uint beforeWrap = capacity-index;
    const v4sf* buffer = (v4sf*)flac.audio.data;
    constexpr uint unroll = 4; // 4*4*4~64bytes: 1 prefetch/line
    v4sf accumulators[unroll]={}; // Breaks dependency chain to pipeline the unrolled loops (FIXME: double precision accumulation)
    if(size>beforeWrap) { // Wraps
        accumulate<unroll>(accumulators, buffer+index,buffer+capacity); //accumulate until end of buffer
        accumulate<unroll>(accumulators, buffer,buffer+(size-beforeWrap)); //accumulate remaining wrapped part
    } else {
        accumulate<unroll>(accumulators, buffer+index,buffer+(index+size));
    }
    v4sf sum={}; for(uint i: range(unroll)) sum+=accumulators[i];
    return double(sum[0]+sum[1]+sum[2]+sum[3]) / 4 / (1<<16) / (1<<16);
}

// Evaluates level from precomputed average of squares (sqrt( sum_N sq u ) / N = sqrt( sum_n ( sum_n' sq u ) / sq n' ) ) / n)
static constexpr int factor = 256; // Downsample factor of precomputed average of squares
static double level(ref<float> envelope, size_t start, size_t duration) {
    start /= factor, duration /= factor;
    double sum = 0;
    if(start < envelope.size) for(float U: envelope.slice(start, min(envelope.size-start, duration))) sum += U;
    return sqrt(sum) / duration;
}

Sampler::Sampler(string path, const uint periodSize, function<void(uint)> timeChanged, Thread& thread)
    : Poll(0, POLLIN, thread), periodSize(periodSize), timeChanged(timeChanged) {
    sampler = this; // DEBUG
    // Parses sfz and map samples
    TextData s = readFile(path);
    Folder folder = section(path,'/',0,-2);
    Sample group, region; Sample* sample=&group;
    for(;;) {
        s.whileAny(" \n\r"_);
        if(!s) break;
        if(s.match("<group>"_)) {
            assert(!group.data);
            if(sample==&region) samples.insertSorted(move(region));
            group=Sample(); sample = &group;
        }
        else if(s.match("<region>"_)) {
            assert(!group.data);
            if(sample==&region) samples.insertSorted(move(region));
            region=move(group); sample = &region;
        }
        else if(s.match("//"_)) {
            s.untilAny("\n\r"_);
            s.whileAny("\n\r"_);
        }
        else {
            string key = s.until('=');
            s.whileAny(" "_);
            string value = s.untilAny(" \n\r"_);
            if(key=="sample"_) {
                String path = replace(replace(value,"\\"_,"/"_),".wav"_,".flac"_);
                sample->name = copy(path);
                sample->data = Map(path, folder, Map::Read, Map::Flags(Map::Shared/*|Map::Populate*/));
                sample->flac = FLAC(sample->data);
                if(!rate) rate=sample->flac.rate;
                else if(rate!=sample->flac.rate) error("Sample rate mismatch", rate,sample->flac.rate);
            }
            else if(key=="trigger"_) { if(value=="release"_) sample->trigger = 1; else error("Unknown trigger",value); }
            else if(key=="lovel"_) sample->lovel=parseInteger(value);
            else if(key=="hivel"_) sample->hivel=parseInteger(value);
            else if(key=="locc64"_) sample->locc64=parseInteger(value);
            else if(key=="hicc64"_) sample->hicc64=parseInteger(value);
            else if(key=="lokey"_) sample->lokey=isInteger(value)?parseInteger(value):noteToMIDI(value);
            else if(key=="hikey"_) sample->hikey=isInteger(value)?parseInteger(value):noteToMIDI(value);
            else if(key=="hirand"_ && value=="0.500") sample->random=0;
            else if(key=="lorand"_ && value=="0.501") sample->random=1;
            else if(key=="pitch_keycenter"_) sample->pitch_keycenter=isInteger(value)?parseInteger(value):noteToMIDI(value);
            else if(key=="key"_) sample->lokey=sample->hikey=sample->pitch_keycenter=isInteger(value)?parseInteger(value):noteToMIDI(value);
            else if(key=="ampeg_release"_) sample->releaseTime = parseDecimal(value);
            else if(key=="amp_veltrack"_) {} /*sample->amp_veltrack=fromDecimal(value)/100;*/
            else if(key=="rt_decay"_) sample->rt_decay = parseInteger(value);
            else if(key=="volume"_) sample->volume = exp10(parseDecimal(value)/20.0); // dB (energy) to amplitude
            else if(key=="tune"_) {} //FIXME
            else if(key=="ampeg_attack"_) {} //FIXME
            else if(key=="ampeg_vel2attack"_) {} //FIXME
            else if(key=="fil_type"_) {} //FIXME
            else if(key=="cutoff"_) {} //FIXME
            else error("Unknown opcode"_,key);
        }
    }
    if(sample==&region) samples.insertSorted(move(region)); // Do not forget to record last region

    Folder("Envelope"_, folder, true);
    for(Sample& s: samples) {
        //log(s.name);
        if(!existsFile(String("Envelope/"_+s.name+".env"_),folder)) {
            FLAC flac(s.data);
            array<float> envelope; uint size=0;
            while(flac.blockSize!=0) {
                while(flac.blockSize!=0 && size<factor) { size+=flac.blockSize; flac.decodeFrame(); }
                if(size>=factor) {
                    envelope.append( sumOfSquares(flac, factor) / sq(factor) );
                    flac.readIndex=(flac.readIndex+factor)%flac.audio.capacity;
                    size-=factor;
                }
            }
            log(s.name, envelope.size);
            writeFile("Envelope/"_+s.name+".env"_,cast<byte>(envelope), folder, true);
        }
        s.envelope = cast<float>(readFile(String("Envelope/"_+s.name+".env"_),folder));
        s.startLevel = ::level(s.envelope, 0, 2048);
        for(size_t t: reverse_range(s.envelope.size)) if(sqrt(s.envelope[t])>0x1p-16) { s.decayTime = t * factor; break; }
        //log(s.decayTime, s.flac.duration);

        /*while(s.flac.audioAvailable+s.flac.blockSize < s.flac.audio.capacity)*/
        s.flac.decodeFrame(); // Decodes first frame of all samples to start mixing without latency
        s.data.lock(); // Locks compressed samples in memory

        for(int key: range(s.lokey,s.hikey+1)) { // Instantiates all pitch shifts on startup
            float shift = key-s.pitch_keycenter; //TODO: tune
            assert_(shift == -1 || shift== 0 || shift == 1, s.lokey, s.hikey, s.pitch_keycenter);
            Layer* layer=0;
            for(Layer& l : layers) if(l.shift==shift) layer=&l;
            if(layer == 0) { // Generates pitch shifting (resampling) filter banks
                Layer layer;
                layer.shift = shift;
                layer.notes.reserve(64);
                if(shift /*|| rate!=outputRate*/) {
                    const uint size = 2048; // Accurate frequency resolution while keeping reasonnable filter bank size
                    layer.resampler = Resampler(2, size, round(size*exp2((-shift)/12.0)/**outputRate/rate*/),
                                                /*readSize:*/ periodSize, /*writeSize:*/ 0/*FLAC::maxBlockSize*/ /*TODO: no copy*/);
                }
                layers.append(move(layer));
            }
        }
    }

    if(&thread != &mainThread) registerPoll();
}

/// Input events (realtime thread)


float Note::actualLevel(uint duration) const { return ::level(envelope, flac.position, duration); }


void Sampler::ccEvent(uint key, uint value) {
 if(key==64) cc64 = value;
}
 void Sampler::noteEvent(uint key, uint velocity, float2 gain) {
    //TODO: Pedal events
    Note* released=0;
    //if(velocity==0) { // Do not release repetitions (i.e needs as many releases as press as if playing multiple instruments) // Also releases repetitions
    for(Layer& layer: layers) for(Note& note: layer.notes) if(note.key==key) {
        if(velocity==0) released=&note; // Triggers release sample (only release, not on repetitions)
        if(note.releaseTime) { // Releases fades out current note
            float step = pow(0x1p-8, 2./*2 frames/step*//(rate*note.releaseTime)); // Fades out to 2^-16 in releaseTime
            //Locker lock(note.lock); // Locks mixer (no mid mix parameter change) (unecessary as notes specialize loop on step anyway, and parameters should get copied in registers)
            note.step = (v4sf){step,step,step,step};
            note.level[2] = note.level[3] = note.level[0] * step; // Steps level of second frame
        }
    }
    if(velocity==0) {
        if(!released) return; // Already fully decayed
        velocity = released->velocity;
    }
    //}
    uint random = this->random%2;
    for(const Sample& s : samples) {
        if(s.trigger == (released?1:0) && s.lokey <= key && key <= s.hikey && s.lovel <= velocity && velocity <= s.hivel && s.locc64 <= cc64 && cc64 < s.hicc64 && random==s.random) {
            float2 level = 1;
            if(released) { // Release (rt_decay is unreliable, matching levels works better), FIXME: window length for energy evaluation is arbitrary
                float releaseLevel = s.startLevel; // 42ms
                float releasedLevel = released->actualLevel(2048);
                float2 currentAttenuation (released->level[0], released->level[1]);
                float2 estimatedLevel = min<float2>(1.f, currentAttenuation * releasedLevel / releaseLevel); // 341ms/21ms
                level = estimatedLevel; //min(estimatedLevel, modeledLevel);
            }
            if(level < float2(0x1p-7)) return;

            float shift = int(key)-s.pitch_keycenter; //TODO: tune
            Layer* layer=0;
            for(Layer& l : layers) if(l.shift==shift) layer=&l;
            assert(layer);

            Note note (::copy(s.flac));
            note.envelope = s.envelope;
            note.decayTime = s.decayTime;
            if(!released) { // Press
                note.key = key, note.velocity = velocity;
                note.level = float4(1); // Velocity layers already select correct level
                //note.level = float4(s.volume * float(velocity) / s.hivel); // E ~ A^2 ~ v^2 => A ~ v (TODO: normalize levels)
                //note.level = float4(s.volume * float(velocity) / 127/*s.hivel*/); // FIXME: this is not right
                note.level[0] *= gain[0];
                note.level[1] *= gain[1];
                note.level[2] *= gain[0];
                note.level[3] *= gain[1];
                //log(velocity, s.lovel, s.hivel, note.level[0]);
            } else {
                note.level = {level[0], level[1], level[0], level[1]};
            }
            note.step=(v4sf){1,1,1,1};
            note.releaseTime=s.releaseTime;
            note.envelope=s.envelope;
            //if(note.flac.sampleSize==16) note.level *= float4(0x1p8f);

            if(layer->notes.size >= layer->notes.capacity) log(layer->notes.size >= layer->notes.capacity, layer->notes.size, layer->notes.capacity);
            else {
                //{Locker lock(notesSizeLock); // Locks note cleanup (assumes note cleanup is only done in this same thread)
                layer->notes.append(move(note)); // Copies predecoded buffer and corresponding FLAC decoder state
                //}
                queue(); //queue background decoder in main thread
            }
            return;
        }
    }
    if(released) return; // Release samples are not mandatory
    if(key<=30 || key>=90) return; // Some instruments have a narrow range
    log("Missing sample"_, key, velocity);
}

/// Background decoder (background thread)

void Sampler::event() { // Decode thread event posted every period from Sampler::read by audio thread
    for(;;) {
        Locker lock(noteReferencesLock); // Locks note cleanup while decoding
        Note* note=0; size_t minBufferSize=-1;
        for(Layer& layer: layers) {
            for(Note& n: layer.notes) { // Finds least buffered note
                if(n.flac.blockSize /*!EOF*/ && n.writeCount >= n.flac.blockSize && n.flac.audioAvailable < minBufferSize) {
                    note = &n;
                    minBufferSize = max<int>(layer.resampler?layer.resampler.need(periodSize):periodSize, n.flac.audioAvailable);
                }
            }
        }
        if(!note) break; // All notes are already fully buffered
        //while(n.flac.audioAvailable < minBufferSize) { // Will be selected again on next iteration otherwise anyway
        size_t size = note->flac.blockSize;
        note->writeCount.acquire(size);
        note->flac.decodeFrame();
        note->readCount.release(size);
        //}
    }
    /*Locker lock1(notesSizeLock); // FIXME: locks new notes while waiting for decoder lock
        Locker lock2(noteReferencesLock);
    // Cleanups silent notes
        for(Layer& layer: layers) layer.notes.filter([this](const Note& note) {
                return (note.flac.blockSize==0 && note.readCount<uint64(2*periodSize)) || note.level[0]<0x1p-7 || note.flac.position > note.decayTime;
        });*/
}

/// Audio mixer (realtime thread)
inline v4sf mix(v4sf level, v4sf step, mref<float2> output, ref<float2> input) {
    size_t size = output.size;
    v4sf* out = (v4sf*)output.data;
    assert_(ptr(out)%sizeof(v4sf)==0);
    const v4sf* in = (const v4sf*)input.data;
    assert_(ptr(in)%sizeof(v4sf)==0, in);
    if(step[0] == 1) for(size_t i: range(size/2)) out[i] += level * in[i];
    else for(size_t i: range(size/2)) { out[i] += level * in[i]; level *= step; }
    return level;
}
void Note::read(mref<float2> output) {
    if(flac.blockSize==0 && readCount<output.size) { readCount.counter=0; return; } // End of stream
    //assert_(readCount + writeCount == flac.audio.capacity, readCount, writeCount, flac.audio.capacity);
    if(!readCount.tryAcquire(output.size)) {
        if(!sampler || sampler->realTime) log("Decoder underrun", output.size, readCount.counter, flac.position, flac.duration, sampler->layers[0].notes.size);
        readCount.acquire(output.size); // Ensures decoder follows
    }
    uint beforeWrap = flac.audio.capacity - flac.readIndex;
    assert_(output.size <= flac.audioAvailable || flac.blockSize == 0, output.size, flac.audioAvailable, readCount);
    size_t size = min(output.size, flac.audioAvailable/2*2);
    assert_(size%2 == 0, size, output.size, flac.audioAvailable);
    if(size > beforeWrap) {
        level = mix(level, step, output.slice(0, beforeWrap), flac.audio.slice(flac.readIndex, beforeWrap));
        level = mix(level, step, output.slice(beforeWrap), flac.audio.slice(0,size-beforeWrap));
        flac.readIndex = size-beforeWrap;
    } else {
        level = mix(level, step, output, flac.audio.slice(flac.readIndex, size));
        flac.readIndex += size;
    }
    __sync_sub_and_fetch(&flac.audioAvailable, size);
    writeCount.release(size); // Allows decoder to continue
    //assert_(readCount + writeCount == flac.audio.capacity, readCount, writeCount, flac.audio.capacity);
    flac.position += size; // Keeps track of position for release sample level matching
}

size_t Sampler::read16(mref<short2> output) {
#if 0
    buffer<float> buffer (output.size);
    size_t size = read(mcast<float2>(buffer));
    for(size_t i: range(size*channels)) output[i] = round(buffer[i]*0x1p-3f); // 16bit -> 16bit with 3bit headroom for multiple notes
#else
    float buffer_[output.size*2]; mref<float2> buffer((float2*)buffer_, output.size);
    size_t size = read(buffer);
    assert_(size==output.size);
    for(size_t i: range(size)) for(size_t c: range(channels)) {
        float u = buffer[i][c];
        if(u<minValue) { minValue=u; log(minValue, maxValue); }
        if(u>maxValue) { maxValue=u; log(minValue, maxValue); }
        float v = (u-minValue) / (maxValue-minValue); // Normalizes range to [0-1] //((u-minValue) / (maxValue-minValue)) * 2 - 1; // Normalizes range to [-1-1]
        int w = v*0x1p16 - 0x1p15; // Converts floating point to two-complement signed 16 bit integer
        output[i][c] = w;
    }
#endif
    return size;
}

size_t Sampler::read32(mref<int2> output) { // Audio thread
#if 0
    v4sf buffer[output.size*2/4];
    uint size = read(mref<float2>((float2*)buffer, output.size));
    for(uint i: range(size*2/4)) ((v4si*)output.data)[i] = cvtps2dq(buffer[i]*float4(0x1p13f)); // 16bit -> 32bit with 3bit headroom for multiple notes
#else
    float buffer_[output.size*2]; mref<float2> buffer((float2*)buffer_, output.size);
    size_t size = read(buffer);
    assert_(size==output.size);
    for(size_t i: range(size)) for(size_t c: range(channels)) {
        float u = buffer[i][c];
        if(u<minValue) { minValue=u; log(minValue, maxValue); }
        if(u>maxValue) { maxValue=u; log(minValue, maxValue); }
        float v = (u-minValue) / (maxValue-minValue); // Normalizes range to [0-1] //((u-minValue) / (maxValue-minValue)) * 2 - 1; // Normalizes range to [-1-1]
        int w = v*0x1p32 - 0x1p31; // Converts floating point to two-complement signed 32 bit integer
        output[i][c] = w;
    }
#endif
    return size;
}

size_t Sampler::read(mref<float2> output) {
    output.clear(0);
    int noteCount = 0;
    {
        if(timeChanged) timeChanged(audioTime); // Updates active notes
        for(Layer& layer: layers) { // Mixes all notes of all layers
            {Locker lock (noteReferencesLock); // Locks decoder
                //Locker lock (notesSizeLock) // Assumes new notes are only added in this mixer thread
                // Cleanups silent notes
                layer.notes.filter([this](const Note& note) {
                    return (note.flac.blockSize==0 && note.readCount<uint64(periodSize)) || note.level[0]<0x1p-7 || note.flac.position > note.decayTime;
                });
            }
            if(layer.resampler) {
                error("");
                int need = layer.resampler.need(output.size);
                if(need >= 0) {
                    size_t inSize = align(2, need);
                    if(layer.audio.capacity<inSize) layer.audio = buffer<float2>(inSize);
                    layer.audio.size = inSize;
                    layer.audio.clear(0);
                    for(Note& note: layer.notes) note.read(layer.audio);
                    layer.resampler.write(cast<float>(layer.audio));
                }
                layer.resampler.read<true>(mcast<float>(output));
            } else {
                for(Note& note: layer.notes) {
                    if(Poll::thread.tid == gettid()) { // Synchronous decoder
                        while(note.flac.blockSize && note.readCount < output.size) {
                            size_t size = note.flac.blockSize;
                            note.writeCount.acquire(size);
                            note.flac.decodeFrame();
                            note.readCount.release(size);
                        }
                    }
                    note.read(output);
                }
            }
            noteCount += layer.notes.size;
        }
    }
    if(noteCount==0) { // Stops audio output when all notes are released
        if(!stopTime) stopTime=audioTime; // First waits for reverb
        else if(audioTime>stopTime) {
            stopTime=0;
            silence = true; //silence();
            //return 0; // Stops audio output (will be restarted on noteEvent (cf music.cc)) (FIXME: disable on video record)
        }
    } else { stopTime=0; silence = false; }
    audioTime += output.size;
    queue(); // Decodes before mixing
    return output.size;
}

Sampler::~Sampler() {}
