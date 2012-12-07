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

    // Predecode 4K (10ms) and lock compressed samples in memory
    for(Sample& s: samples) s.cache.decode(1<<12), s.map.lock();

    array<byte> reverbFile = readFile("reverb.flac"_,folder);
    FLAC reverbMedia(reverbFile);
    assert(reverbMedia.rate == 48000);
    reverbSize = reverbMedia.duration/8; log(reverbSize);
    N = reverbSize+periodSize;
    float* stereoFilter = allocate64<float>(2*N); clear(stereoFilter,2*N);
    for(uint i=0;i<reverbSize;) {
        uint read = reverbMedia.read((float2*)stereoFilter+i,reverbSize-i);
        i+=read;
    }

    // Computes normalization
    float sum=0; for(uint i: range(2*reverbSize)) sum += stereoFilter[i]*stereoFilter[i];
    const float scale = 0x1p5f/sqrt(sum); //8 (24bit->32bit) - 3 (head room)

    // Reverses, scales and deinterleaves filter
    float* filter[2];
    for(int c=0;c<2;c++) filter[c] = allocate64<float>(N), clear(filter[c],N);
    for(uint i: range(reverbSize)) {
        for(int c=0;c<2;c++) filter[c][N-1-i] = scale*((float*)stereoFilter)[2*i+c];
    }

    //DEBUG
    for(int c=0;c<2;c++) {
        clear(filter[c],N);
        filter[c][N-1] = 1;
        //filter[c][0] = 1;
    }

    // Transform reverb filter to frequency domain
    for(int c=0;c<2;c++) {
        reverbFilter[c] = allocate64<float>(N); clear(reverbFilter[c],N);
        fftwf_plan p = fftwf_plan_r2r_1d(N, filter[c], reverbFilter[c], FFTW_R2HC, FFTW_ESTIMATE);
        //{string s; for(uint i: range(N)) s<<str(filter[c][i])<<'\t'; log("f",s);}
        fftwf_execute(p);
        //{string s; for(uint i: range(N)) s<<str(reverbFilter[c][i])<<'\t'; log("F",s);}
        fftwf_destroy_plan(p);
        unallocate(filter[c],N); // release time domain filter
    }

    // Allocates reverb buffer and temporary buffers
    buffer = allocate64<float>(periodSize*2);
    input = allocate64<float>(N);
    for(int c=0;c<2;c++) {
        reverbBuffer[c] = allocate64<float>(N), clear(reverbBuffer[c],N);
        forward[c] = fftwf_plan_r2r_1d(N, reverbBuffer[c], input, FFTW_R2HC, FFTW_ESTIMATE);
    }
    product = allocate64<float>(N);
    backward = fftwf_plan_r2r_1d(N, product, input, FFTW_HC2R, FFTW_ESTIMATE);
    //reverbIndex = periodSize; //TODO: ring buffer
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
                if(notes.size()==notes.capacity()) { error("too many ",notes.capacity(),"active notes"); return;}
                notes << move(note);
            }
            queue(); //queue background decoder in main thread
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
bool Sampler::read(ptr& swPointer, int32* output, uint size unused) { // Audio thread
    {Locker lock(noteReadLock);
        if(size!=periodSize) error(size,periodSize);
        if(reverbSize%8) error("reverbSize",reverbSize);

        // Setups mixing buffer
        clear(buffer, periodSize*2);
        // Mixes notes which don't need any resampling
        for(Note& note: notes[1]) note.read((float4*)buffer, periodSize/2);
        // Mixes pitch shifting layers
        for(int i=0;i<2;i++) {
            uint inSize=align(2,resampler[i].need(periodSize));
            float4* layer = allocate64<float4>(inSize); clear(layer, inSize/2);
            for(Note& note: notes[i*2]) note.read(layer, inSize/2);
            resampler[i].filter<true>((float*)layer, inSize, buffer, periodSize);
            unallocate(layer,inSize/2);
        }

#if 1
        // Applies convolution reverb
        //TODO if(reverbIndex==reverbSize) reverbIndex=0; reverbIndex += periodSize; // Wrap buffer with periodSize overlap

        // Deinterleaves mixed signal into reverb buffer
        for(uint i: range(periodSize)) {
            for(int c=0;c<2;c++) reverbBuffer[c][reverbSize+i] = buffer[2*i+c]; //TODO: ring buffer (reverbIndex)
        }

        for(int c=0;c<2;c++) {
            // Transforms reverb buffer to frequency-domain ( reverbBuffer -> input )
            fftwf_execute(forward[c]);

            /*{float8* reverb = (float8*)reverbBuffer[c];
                for(uint i: range(reverbSize/8)) reverb[i] = reverb[i+periodSize/8]; // Shifts buffer for next frame (FIXME: ring buffer)
            }*/
            for(uint i: range(reverbSize)) reverbBuffer[c][i] = reverbBuffer[c][i+periodSize]; // Shifts buffer for next frame

            // Complex multiplies input (reverb buffer) with kernel (reverb filter)
#if 1
            clear(product,N); //FIXME
            float* x = input;
            float* y = reverbFilter[c];
            product[0] = x[0] * y[0];
            for(uint j = 1; j < N/2; j++) {
                float a = x[j];
                float b = x[N - j];
                float c = y[j];
                float d = y[N - j];
                if(product[j] != 0) error(j,product[j]);
                if(product[N-j] != 0) error(j,product[N-j]);
                product[j] = a*c-b*d; // Re
                product[N - j] = a*d+b*c; // Im
                //assert(!__builtin_isnan(product[j])); assert(!__builtin_isnan(product[N - j]));
            }
            product[N/2] = x[N/2] * y[N/2];
#elif 1
            {
                float* original[2] = {input, reverbFilter[c]};
                float* buffer[2];
                for(int i=0;i<2;i++) {
                    buffer[i] = allocate64<float>(N);
                    float* orig = original[i];
                    float* out = buffer[i];
                    out[0] = orig[0];
                    out[1] = orig[N/2];
                    for(uint t=1; t<N/2; t++) {
                        out[2*t+0] = orig[t];
                        out[2*t+1] = orig[N-t];
                    }
                }
                float* x = buffer[0];
                float* y = buffer[1];
                clear(product,N); //FIXME
                float* out = product;
                out[0] = x[0] * y[0];
                //out[1] = x[1] * y[1];
                for(uint j = 1; j < N/2; j++) {
                    float a = x[j*2];
                    float b = x[j*2+1];
                    float c = y[j*2];
                    float d = y[j*2+1];
#if 0
                    out[j*2] += a*c-b*d; // Re
                    out[j*2+1] += a*d+b*c; // Im
#else
                    out[j] += a*c-b*d; // Re
                    out[N-j] += a*d+b*c; // Im
#endif
                }
                out[N/2] = x[1] * y[1]; // r(N/2)
                for(int i=0;i<2;i++) unallocate(buffer[i],N);
            }
#else
            for(uint i: range(N)) product[i] = input[i];
#endif
            /*for(uint i: range(N)) {
                if(product[i] != input[i]) {
                    string s1; for(uint i: range(periodSize)) s1<<str(product[i])<<'\t'; log("S1",s1);
                    string s2; for(uint i: range(periodSize)) s2<<str(input[i])<<'\t'; log("S2",s2);
                    string s3; for(uint i: range(periodSize)) s3<<str(product[i]/input[i])<<'\t'; log("S3",s3);
                    error(i,product[i],input[i],product[i]/input[i]);
                }
            }*/

            // Transforms product back to time-domain ( product -> input )
            fftwf_execute(backward);

            for(uint i: range(periodSize)) { // Normalizes and writes samples back in output buffer
                float a=buffer[2*i+c], b=(1.f/N)*input[reverbSize-1+i];
                if(abs(a-b)>2) {
                    string s1; for(uint i: range(periodSize)) s1<<str(buffer[2*i+c])<<'\t'; log("S1",s1);
                    string s2; for(uint i: range(periodSize)) s2<<str((1.f/N)*input[reverbSize-1+i])<<'\t'; log("S2",s2);
                    string s3; for(uint i: range(periodSize)) s3<<str((((1.f/N)*input[reverbSize-1+i]))/buffer[2*i+c])<<'\t'; log("S3",s3);
                    error(c,i,a,b);
                }
                buffer[2*i+c] = (1.f/N)*input[reverbSize-1+i]; //TODO: ring buffer (reverbIndex)
            }
        }
#endif
        // Scales to 32bit
        for(uint i: range(periodSize*2)) buffer[i]*=0x1p8f;
        // Converts mixing buffer to signed 32bit output
        for(uint i: range(periodSize/4)) ((word8*)output)[i] = cvtps(((float8*)buffer)[i]);

        swPointer += periodSize;
    }
    time+=periodSize;
    queue(); //queue background decoder in main thread
    //if(record) record.write(ref<byte>((byte*)output,periodSize*sizeof(int32)));
    return true;
}

void Sampler::recordWAV(const ref<byte>& path) {
    error("Recording unsupported");
    record = File(path,home(),WriteOnly);
    struct { char RIFF[4]={'R','I','F','F'}; int32 size; char WAVE[4]={'W','A','V','E'}; char fmt[4]={'f','m','t',' '};
        int32 headerSize=16; int16 compression=1; int16 channels=2; int32 rate=48000; int32 bps=48000*4;
        int16 stride=4; int16 bitdepth=16; char data[4]={'d','a','t','a'}; /*size?*/ } _packed header;
    record.write(raw(header));
}
Sampler::~Sampler() {
    for(uint c=0;c<2;c++) {
        if(reverbFilter[c]) unallocate(reverbFilter[c],N);
        if(reverbBuffer[c]) unallocate(reverbBuffer[c],N);
        fftwf_destroy_plan(forward[c]);
    }
    fftwf_destroy_plan(backward);
    unallocate(input,N);
    unallocate(product,N);
    unallocate(buffer,periodSize/4);
    if(!record) return;
    error("Recording unsupported");
    //record.seek(4); write(record,raw<int32>(36+time));
}
constexpr uint Sampler::periodSize;
