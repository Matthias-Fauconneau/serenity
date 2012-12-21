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

inline string str(const Sample& s) { return str(s.lokey)+"-"_+str(s.pitch_keycenter)+"-"_+str(s.hikey); }

/// SFZ

int noteToMIDI(const ref<byte>& value) {
    int note=24;
    int i=0;
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

void Sampler::open(const ref<byte>& path) {
    // parse sfz and map samples
    Sample group;
    TextData s = readFile(path);
    Folder folder = section(path,'.',0,-2); // Samples are implicilty in a subfolder with same name as .sfz files
    Sample* sample=&group;
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
                string path = replace(replace(value,"\\"_,"/"_),".wav"_,".flac"_);
                sample->map = Map(path,folder);
                sample->data = Note(sample->map);
                if(!existsFile(string(path+".env"_),folder)) {
                    Note copy = sample->data;
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
                sample->data.envelope = sample->envelope;
                if(!rate) rate=sample->data.rate;
                else if(rate!=sample->data.rate) error("Sample rate mismatch",rate,sample->data.rate);
            }
            else if(key=="trigger"_) { if(value=="release"_) sample->trigger = 1; else warn("unknown trigger",value); }
            else if(key=="lovel"_) sample->lovel=toInteger(value);
            else if(key=="hivel"_) sample->hivel=toInteger(value);
            else if(key=="lokey"_) sample->lokey=isInteger(value)?toInteger(value):noteToMIDI(value);
            else if(key=="hikey"_) sample->hikey=isInteger(value)?toInteger(value):noteToMIDI(value);
            else if(key=="pitch_keycenter"_) {
                sample->pitch_keycenter=isInteger(value)?toInteger(value):noteToMIDI(value);
                assert(sample->pitch_keycenter>=sample->lokey,sample->pitch_keycenter,value);
                assert(sample->pitch_keycenter<=sample->hikey,sample->pitch_keycenter,value);
            }
            else if(key=="key"_) sample->lokey=sample->hikey=sample->pitch_keycenter=isInteger(value)?toInteger(value):noteToMIDI(value);
            else if(key=="ampeg_release"_) sample->releaseTime=toDecimal(value);
            else if(key=="amp_veltrack"_) sample->amp_veltrack=toInteger(value);
            else if(key=="rt_decay"_) {}//sample->rt_decay=toInteger(value);
            else if(key=="volume"_) sample->volume= exp10(toDecimal(value)/20.0);
            else if(key=="tune"_) {} //FIXME
            else if(key=="ampeg_attack"_) {} //FIXME
            else if(key=="ampeg_vel2attack"_) {} //FIXME
            else if(key=="fil_type"_) {} //FIXME
            else if(key=="cutoff"_) {} //FIXME
            else error("Unknown opcode"_,key);
        }
    }

    for(Sample& s: samples) {
        s.data.decode(1<<12); // Predecodes 4K (10ms)
        s.map.lock(); // Locks compressed samples in memory

        for(int key: range(s.lokey,s.hikey+1)) { // Instantiates all pitch shifts on startup
            float shift = key-s.pitch_keycenter; //TODO: tune
            Layer* layer=0;
            for(Layer& l : layers) if(l.shift==shift) layer=&l;
            if(layer == 0) { // Generates pitch shifting (resampling) filter banks
                layers.grow(layers.size()+1);
                layer = &layers.last();
                layer->shift = shift;
                layer->notes.reserve(64); //Avoid locking (FIXME: heap pointers array)
                if(shift) {
                    uint size = max(periodSize*2,1024u);
                    new (&layer->resampler) Resampler(2, size, round(size*exp2((-shift)/12.0)));
                }
            }
        }
    }

    array<byte> reverbFile = readFile("../reverb.flac"_,folder);
    FLAC reverbMedia(reverbFile);
    assert(reverbMedia.rate == rate);
    reverbSize = reverbMedia.duration;
    N = reverbSize+periodSize;
    float* stereoFilter = allocate64<float>(2*reverbSize); clear(stereoFilter,2*reverbSize);
    for(uint i=0;i<reverbSize;) {
        uint read = reverbMedia.read((float2*)(stereoFilter+2*i),min(1u<<16,reverbSize-i));
        i+=read;
    }

    // Computes normalization
    float sum=0; for(uint i: range(2*reverbSize)) sum += stereoFilter[i]*stereoFilter[i];
    const float scale = 0x1p6f/sqrt(sum); // Normalizes and scales 24->32bit (-3bit head room)

    // Reverses, scales and deinterleaves filter
    float* filter[2];
    for(int c=0;c<2;c++) filter[c] = allocate64<float>(N), clear(filter[c],N);
    for(uint i: range(reverbSize)) for(int c=0;c<2;c++) filter[c][i] = scale*stereoFilter[2*i+c];

    fftwf_init_threads();
    fftwf_plan_with_nthreads(4);
    //fftwf_import_wisdom_from_filename("/Samples/wisdom");

    // Transforms reverb filter to frequency domain
    for(int c=0;c<2;c++) {
        reverbFilter[c] = allocate64<float>(N); clear(reverbFilter[c],N);
        fftwf_plan p = fftwf_plan_r2r_1d(N, filter[c], reverbFilter[c], FFTW_R2HC, FFTW_ESTIMATE);
        fftwf_execute(p);
        fftwf_destroy_plan(p);
        unallocate(filter[c],N); // Releases time domain filter
        for(uint i: range(N/2-N/4,N/2+N/4)) reverbFilter[c][i]=0; //Cuts frequencies higher than nyquist (FIXME: is this smart?)
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

    //fftwf_export_wisdom_to_filename("/Samples/wisdom");
}

/// Input events (realtime thread)

float Note::actualLevel(uint size) const {
    const int count = size>>8;
    if((position>>8)+count>envelope.size) return 0; //fully decayed
    float sum=0; for(int i=0;i<count;i++) sum+=envelope[(position>>8)+count]; //precomputed sum
    return sqrt(sum)/size;
}

void Sampler::noteEvent(uint key, uint velocity) {
    Note* current=0;
    if(velocity==0) {
        for(Layer& layer: layers) for(Note& note: layer.notes) if(note.key==key) {
            current=&note; //schedule release sample
            //release fade out current note
            float step = pow(1.0/(1<<24),(/*2 samples/step*/2.0/(rate*note.releaseTime)));
            note.step=(float4)__(step,step,step,step);
        }
        if(!current) return; //already fully decayed
    }
    for(const Sample& s : samples) {
        if(s.trigger == (current?1:0) && s.lokey <= key && key <= s.hikey && s.lovel <= velocity && velocity <= s.hivel) {
            float level;
            if(current) { //rt_decay is unreliable, matching levels works better
                level = current->actualLevel(1<<14) / s.data.actualLevel(1<<11);
                level *= extract(current->level,0);
                if(level>8) level=8;
            } else {
                level=(1-(s.amp_veltrack/100.0*(1-(velocity*velocity)/(127.0*127.0)))) * s.volume;
            }
            if(level<0x1p-15) return;
            Note note = s.data;
            if(!current) note.key=key;
            note.step=(float4)__(1,1,1,1);
            note.releaseTime=s.releaseTime;
            note.envelope=s.envelope;
            if(note.sampleSize==16) level*=0x1p8;
            note.level=(float4)__(level,level,level,level);
            {Locker lock(noteReadLock);
                float shift = int(key)-s.pitch_keycenter; //TODO: tune
                Layer* layer=0;
                for(Layer& l : layers) if(l.shift==shift) layer=&l;
                if(layer == 0) { error("Layer not instantiated at initialization",key, s.lokey, s.hikey, s.pitch_keycenter, shift); return; }
                if(layer->notes.size()==layer->notes.capacity()) {
                    Locker lock(noteWriteLock); // Need to lock decoder for reallocation (FIXME: use a list of heap pointer instead)
                    log(layer->notes.size());
                    layer->notes.reserve(layer->notes.size()*2);
                }
                layer->notes << move(note);
            }
            queue(); //queue background decoder in main thread
            return;
        }
    }
    if(current) return; //release samples are not mandatory
    log("Missing sample"_,key);
    log(samples);
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
        for(Layer& layer: layers) for(uint j=0; j<layer.notes.size(); j++) { Note& note=layer.notes[j];
            if((note.blockSize==0 && note.readCount<2*128) || extract(note.level,0)<0x1p-23) layer.notes.removeAt(j); else j++;
        }
        noteReadLock.unlock();
    }
    Locker lock(noteWriteLock);
    for(;;) {
        Note* note=0; uint minBufferSize=-1;
        for(Layer& layer: layers) for(Note& n: layer.notes) { // find least buffered note
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
bool Sampler::read(int32* output, uint size) { // Audio thread
    {Locker lock(noteReadLock);
        if(size!=periodSize) error(size,periodSize);

        clear(buffer, periodSize*2);
        for(Layer& layer: layers) { // Mixes all notes of all layers
            if(layer.resampler) {
                uint inSize=align(2,layer.resampler.need(periodSize));
                if(layer.buffer.capacity<inSize*2) layer.buffer = Buffer<float>(inSize*2);
                clear(layer.buffer.data, inSize*2);
                for(Note& note: layer.notes) note.read((float4*)layer.buffer.data, inSize/2);
                layer.resampler.filter<true>(layer.buffer, inSize, buffer, periodSize);
            } else {
                 for(Note& note: layer.notes) note.read((float4*)buffer, periodSize/2);
            }
        }

        if(enableReverb) { // Convolution reverb
            // Deinterleaves mixed signal into reverb buffer
            for(uint i: range(periodSize)) for(int c=0;c<2;c++) reverbBuffer[c][reverbSize+i] = buffer[2*i+c];

            for(int c=0;c<2;c++) {
                // Transforms reverb buffer to frequency-domain ( reverbBuffer -> input )
                fftwf_execute(forward[c]);

                for(uint i: range(reverbSize)) reverbBuffer[c][i] = reverbBuffer[c][i+periodSize]; // Shifts buffer for next frame

                // Complex multiplies input (reverb buffer) with kernel (reverb filter)
                float* x = input;
                float* y = reverbFilter[c];
                product[0] = x[0] * y[0];
                for(uint j = 1; j < N/2; j++) {
                    float a = x[j];
                    float b = x[N - j];
                    float c = y[j];
                    float d = y[N - j];
                    product[j] = a*c-b*d;
                    product[N - j] = a*d+b*c;
                }
                product[N/2] = x[N/2] * y[N/2];

                // Transforms product back to time-domain ( product -> input )
                fftwf_execute(backward);

                for(uint i: range(periodSize)) { // Normalizes and writes samples back in output buffer
                    buffer[2*i+c] = (1.f/N)*input[reverbSize+i];
                }
            }
        } else {
            for(uint i: range(2*periodSize)) buffer[i] *= 0x1p6f;
        }
        // Converts mixing buffer to signed 32bit output
        for(uint i: range(periodSize/4)) ((word8*)output)[i] = cvtps(((float8*)buffer)[i]);
    }
    time+=periodSize;
    queue(); //queue background decoder in main thread
    if(record) record.write(ref<byte>((byte*)output,2*periodSize*sizeof(int32)));
    frameReady(buffer, periodSize);
    return true;
}

void Sampler::startRecord(const ref<byte>& name) {
    string path = name+".wav"_;
    record = File(path,home(),WriteOnly|Create|Truncate);
    struct { char RIFF[4]={'R','I','F','F'}; int32 size; char WAVE[4]={'W','A','V','E'}; char fmt[4]={'f','m','t',' '};
             int32 headerSize=16; int16 compression=1; int16 channels=2; int32 rate=44100; int32 bps=rate*channels*sizeof(int32);
        int16 stride=channels*sizeof(int32); int16 bitdepth=32; char data[4]={'d','a','t','a'}; /*size?*/ } _packed header;
    record.write(raw(header));
    recordStart = time;
}
void Sampler::stopRecord() {
    if(record) { record.seek(4); record.write(raw<int32>(36+time)); record=0; }
}

Sampler::~Sampler() {
    stopRecord();
    for(uint c=0;c<2;c++) {
        if(reverbFilter[c]) unallocate(reverbFilter[c],N);
        if(reverbBuffer[c]) unallocate(reverbBuffer[c],N);
        fftwf_destroy_plan(forward[c]);
    }
    fftwf_destroy_plan(backward);
    unallocate(input,N);
    unallocate(product,N);
    unallocate(buffer,periodSize*2);
}
constexpr uint Sampler::periodSize;
