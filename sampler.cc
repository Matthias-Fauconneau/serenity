#include "sampler.h"
#include "data.h"
#include "file.h"
#include "flac.h"
#include "time.h"
#include "simd.h"
#include "string.h"

/// Floating point operations
inline float exp2(float x) { return __builtin_exp2f(x); }
inline float log2(float x) { return __builtin_log2f(x); }
inline float exp10(float x) { return exp2(x*log2(10)); }
inline float log10(float x) { return log2(x)/log2(10); }
inline float dB(float x) { return 10*log10(x); }
inline float pow(float x, float y) { return __builtin_pow(x,y); }

/// Raw memory copy
typedef int m128 __attribute((vector_size(16)));
void copy16(void* target,const void* source, int size) {
    m128* dst=(m128*)target, *src=(m128*)source, *end=(m128*)source+size;
    constexpr uint unroll=4; assert(size%unroll==0);
    while(src<end) {
        __builtin_prefetch(src+32,0,0);
        __builtin_prefetch(dst+32,0,0);
        for(uint i: range(unroll)) dst[i]=src[i];
        src+=unroll; dst+=unroll;
    }
}

#include "map.h"
uint availableMemory() {
    map<string, uint> info;
    for(TextData s = File("/proc/meminfo"_).readUpTo(4096);s;) {
        ref<byte> key=s.until(':'); s.skip();
        uint value=toInteger(s.untilAny(" \n"_)); s.line();
        info.insert(string(key), value);
    }
    return info.at(string("MemFree"_))+info.at(string("Inactive"_))+info.at(string("Active(file)"_));
}

/// SFZ

void Sampler::open(const ref<byte>& path) {
    // parse sfz and map samples
    Sample group;
    TextData s = readFile(path);
    Folder folder = section(path,'/',0,-2);
    Sample* sample=0;
    for(;;) {
        s.whileAny(" \n\r"_);
        if(!s) break;
        if(s.match("<group>"_)) { group=Sample(); sample = &group; }
        else if(s.match("<region>"_)) { assert(!group.map.data); samples<<move(group); sample = &samples.last();  }
        else if(s.match("//"_)) {
            s.untilAny("\n\r"_);
            s.whileAny("\n\r"_);
        }
        else {
            ref<byte> key = s.until('=');
            ref<byte> value = s.untilAny(" \n\r"_);
            if(key=="sample"_) {
                string path = replace(value,"\\"_,"/"_);
                sample->map = Map(path,folder);
                sample->cache = sample->map;
                if(!existsFile(string(path+".env"_),folder)) {
                    Note copy = sample->cache;
                    array<float> envelope; uint size=0;
                    while(copy.blockSize!=0) {
                        const uint period=1<<8;
                        while(copy.blockSize!=0 && size<period) { size+=copy.blockSize; copy.decodeFrame(); }
                        if(size>=period) { envelope << copy.sumOfSquares(period); copy.readIndex=(copy.readIndex+period)%copy.buffer.capacity; size-=period; }
                    }
                    log(string(path+".env"_),envelope.size());
                    writeFile(string(path+".env"_),cast<byte,float>(envelope),folder);
                }
                sample->envelope = array<float>(cast<float,byte>(readFile(string(path+".env"_),folder)));
                sample->cache.envelope = sample->envelope;
            }
            else if(key=="trigger"_) { if(value=="release"_) sample->trigger = 1; else warn("unknown trigger",value); }
            else if(key=="lovel"_) sample->lovel=toInteger(value);
            else if(key=="hivel"_) sample->hivel=toInteger(value);
            else if(key=="lokey"_) sample->lokey=toInteger(value);
            else if(key=="hikey"_) sample->hikey=toInteger(value);
            else if(key=="pitch_keycenter"_) sample->pitch_keycenter=toInteger(value);
            else if(key=="ampeg_release"_) sample->releaseTime=toInteger(value);
            else if(key=="amp_veltrack"_) sample->amp_veltrack=toInteger(value);
            else if(key=="rt_decay"_) {}//sample->rt_decay=toInteger(value);
            else if(key=="volume"_) sample->volume=toInteger(value);
            else error("Unknown opcode"_,key);
        }
    }

    // Generates pitch shifting (resampling) filter banks
    new (&resampler[0]) Resampler(2, 1024, round(1024*exp2((1)/12.0)));
    new (&resampler[1]) Resampler(2, 1024, round(1024*exp2((-1)/12.0)));
    for(int i=0;i<3;i++) notes[i].reserve(128);

    // Lock compressed samples in memory
    full=0; for(const Sample& s : samples) full += (s.map.size+4095)/4096*4;
    available = availableMemory() - ((samples.size()*(1<<14)*2*sizeof(float))>>10) /*predecoded samples*/ - (10<<10) /*heap*/;
    if(full>available) {
        lock=0; for(const Sample& s : samples) lock += min(s.map.size/4096*4,available*1024/samples.size()/4096*4);
        log("Locking",lock/1024,"MiB of",available/1024,"MiB available memory, full cache need",full/1024,"MiB");
    }
    current=0; queue();
}

/// Input events (realtime thread)

template<int unroll> inline void accumulate(float4 accumulators[unroll], const float4* ptr, const float4* end) {
    for(;ptr<end;ptr+=unroll) {
        __builtin_prefetch(ptr+32,0,0);
        for(uint i: range(unroll)) accumulators[i]+=ptr[i]*ptr[i];
    }
}
float Note::sumOfSquares(uint size) {
    size =size/2; uint index = readIndex/2, capacity = buffer.capacity/2; //align to float4
    uint beforeWrap = capacity-index;
    const float4* buffer = (float4*)this->buffer.data;
    constexpr uint unroll=4; //4*4*4~64bytes: 1 prefetch/line
    float4 accumulators[unroll]={}; //breaks dependency chain to pipeline the unrolled loops
    if(size>beforeWrap) { //wrap
        accumulate<unroll>(accumulators, buffer+index,buffer+capacity); //accumulate until end of buffer
        accumulate<unroll>(accumulators, buffer,buffer+(size-beforeWrap)); //accumulate remaining wrapped part
    } else {
        accumulate<unroll>(accumulators, buffer+index,buffer+(index+size));
    }
    float4 sum={}; for(uint i: range(unroll)) sum+=accumulators[i];
    return (extract(sum,0)+extract(sum,1)+extract(sum,2)+extract(sum,3))/(1<<24)/(1<<24);
}
float Note::actualLevel(uint size) const {
    const int count = size>>8;
    if((position>>8)+count>envelope.size) return 0; //fully decayed
    float sum=0; for(int i=0;i<count;i++) sum+=envelope[(position>>8)+count]; //precomputed sum
    return sqrt(sum)/size;
}

void Sampler::noteEvent(int key, int velocity) {
    Note* current=0;
    if(velocity==0) {
        for(int i=0;i<3;i++) for(Note& note: notes[i]) if(note.key==key) {
            current=&note; //schedule release sample
            if(note.releaseTime) { //release fade out current note
                float step = pow(1.0/(1<<24),(/*2 samples/step*/2.0/(48000*note.releaseTime)));
                note.step=(float4)__(step,step,step,step);
            }
        }
        if(!current) return; //already fully decayed
    }
    for(const Sample& s : samples) {
        if(s.trigger == (current?1:0) && s.lokey <= key && key <= s.hikey && s.lovel <= velocity && velocity <= s.hivel) {
            float level;
            if(current) { //rt_decay is unreliable, matching levels works better
                level = current->actualLevel(1<<14) / s.cache.actualLevel(1<<11);
                level *= extract(current->level,0);
                if(level>8) level=8;
            } else {
                level=(1-(s.amp_veltrack/100.0*(1-(velocity*velocity)/(127.0*127.0)))) * exp10(s.volume/20.0);
            }
            if(level<0x1p-15) return;
            Note note = s.cache;
            if(!current) note.key=key;
            note.step=(float4)__(1,1,1,1);
            note.releaseTime=s.releaseTime;
            note.envelope=s.envelope;
            note.level=(float4)__(level,level,level,level);
            {Locker lock(noteReadLock);
                array<Note>& notes = this->notes[1+key-s.pitch_keycenter];
                if(notes.size()==notes.capacity()) {Locker lock(noteWriteLock); notes.reserve(notes.capacity()+1); log("size",notes.capacity());}
                notes << move(note);
            }
            return;
        }
    }
    if(current) return; //release samples are not mandatory
    error("Missing sample"_,key,velocity);
}

/// Background decoder (background thread)

void Note::decode(uint need) {
    assert(need<=buffer.capacity);
    while(readCount<=int(need) && blockSize && (int)writeCount>=blockSize) {
        int size=blockSize; writeCount.acquire(size); FLAC::decodeFrame(); readCount.release(size);
    }
}
void Sampler::event() { // Main thread event posted every period from Sampler::read by audio thread
    if(noteReadLock.tryLock()) { //Quickly cleanup silent notes
        for(uint i: range(3)) for(uint j=0; j<notes[i].size(); j++) { Note& note=notes[i][j];
            if((note.blockSize==0 && note.readCount<2*128) || extract(note.level,0)<0x1p-15) notes[i].removeAt(j); else j++;
        }
        noteReadLock.unlock();
    }
    Locker lock(noteWriteLock);
    for(;;) {
        Note* note=0; uint minBufferSize=-1;
        for(int i=0;i<3;i++) for(Note& n: notes[i]) { // find least buffered note
            if(n.blockSize && n.writeCount>=n.blockSize && n.buffer.size<minBufferSize) {
                note=&n;
                minBufferSize=n.buffer.size;
            }
        }
        if(!note) break; //all notes are already fully buffered
        int size=note->blockSize; note->writeCount.acquire(size); note->decodeFrame(); note->readCount.release(size);
    }

    if(time>lastTime) { int time=this->time; timeChanged(time-lastTime); lastTime=time; } // read MIDI file / update UI

    if(current<samples.size()) {
        Sample& s=samples[current++];
        uint size = s.map.size;
        if(full>available) size=min(size, available*1024/samples.size());
        current=samples.size();
        debug(current=samples.size(); if(0)) s.cache.decode(1<<14), s.map.lock(size);
        progressChanged(current,samples.size());
        if(current<samples.size()) queue();
    }
}

/// Audio mixer (realtime thread)
inline void mix(float4& level, float4 step, float4* out, float4* in, uint size) { for(uint i: range(size)) { out[i] += level * in[i]; level*=step; } }
void Note::read(float4* out, uint size) {
    if(blockSize==0 && readCount<int(size*2)) { readCount=0; return; } // end of stream
    if(!readCount.tryAcquire(size*2)) {
        log("decoder underrun");
        readCount.acquire(size*2); //ensure decoder follows
    }
    uint beforeWrap = (buffer.capacity-readIndex)/2;
    if(size>beforeWrap) {
        mix(level,step,out,(float4*)(buffer+readIndex),beforeWrap);
        mix(level,step,out+beforeWrap,(float4*)(buffer+0),size-beforeWrap);
        readIndex = (size-beforeWrap)*2;
    } else {
        mix(level,step,out,(float4*)(buffer+readIndex),size);
        readIndex += size*2;
    }
    buffer.size-=size*2; writeCount.release(size*2); //allow decoder to continue
    position+=size*2; //keep track of position for release sample level matching
}
bool Sampler::read(ptr& swPointer, int16* output, uint size) { // Audio thread
    {Locker lock(noteReadLock);
        const uint period=size/2;
        assert(size%2==0);

        float4* buffer = allocate16<float4>(period); clear(buffer, period);
        // Mix notes which don't need any resampling
        for(Note& note: notes[1]) note.read(buffer, period);
        // Mix pitch shifting layers
        for(int i=0;i<2;i++) {
            uint inSize=align(2,resampler[i].need(2*period))/2;
            float4* layer = allocate16<float4>(inSize); clear(layer, inSize);
            for(Note& note: notes[i*2]) note.read(layer, inSize);
            resampler[i].filter<true>((float*)layer, inSize*2, (float*)buffer, period*2);
            unallocate(layer,inSize);
        }

        for(uint i: range(period/2)) {
            ((half8*)output)[i] = packs( sra(cvtps(buffer[i*2+0]),9), sra(cvtps(buffer[i*2+1]),9) ); //8 samples = 4 frames
        }
        unallocate(buffer,period);

        swPointer += size;
    }
    time+=size;
    queue(); //queue background decoder in main thread
    //if(record) record.write(ref<byte>((byte*)output,period*2*sizeof(int16))); time+=period;
    return true;
}

void Sampler::recordWAV(const ref<byte>& path) {
    error("Recording unsupported");
    record = File(path,home(),WriteOnly);
    struct { char RIFF[4]={'R','I','F','F'}; int32 size; char WAVE[4]={'W','A','V','E'}; char fmt[4]={'f','m','t',' '};
        int32 headerSize=16; int16 compression=1; int16 channels=2; int32 rate=48000; int32 bps=48000*4;
        int16 stride=4; int16 bitdepth=16; char data[4]={'d','a','t','a'}; /*size?*/ } packed header;
    record.write(raw(header));
}
Sampler::~Sampler() {
    if(!record) return;
    error("Recording unsupported");
    //record.seek(4); write(record,raw<int32>(36+time));
}
