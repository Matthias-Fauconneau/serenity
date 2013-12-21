#include "simd.h"
#include "sampler.h"
#include "resample.h"
#include "data.h"
#include "file.h"
#include "flac.h"
#include "time.h"
#include "string.h"
#include "math.h"
#include <fftw3.h> //fftw3f
#define FFTW_THREADS 1
#if FFTW_THREADS
#include <fftw3.h> //fftw3f_threads
#endif

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
        for(uint i: range(unroll)) accumulators[i]+=ptr[i]*ptr[i];
    }
}
float sumOfSquares(const FLAC& flac, uint size) {
    size =size/2; uint index = flac.readIndex/2, capacity = flac.audio.capacity/2; //align to v4sf
    uint beforeWrap = capacity-index;
    const v4sf* buffer = (v4sf*)flac.audio.data;
    constexpr uint unroll=4; //4*4*4~64bytes: 1 prefetch/line
    v4sf accumulators[unroll]={}; //breaks dependency chain to pipeline the unrolled loops
    if(size>beforeWrap) { //wrap
        accumulate<unroll>(accumulators, buffer+index,buffer+capacity); //accumulate until end of buffer
        accumulate<unroll>(accumulators, buffer,buffer+(size-beforeWrap)); //accumulate remaining wrapped part
    } else {
        accumulate<unroll>(accumulators, buffer+index,buffer+(index+size));
    }
    v4sf sum={}; for(uint i: range(unroll)) sum+=accumulators[i];
    return (sum[0]+sum[1]+sum[2]+sum[3])/(1<<24)/(1<<24);
}

void Sampler::open(uint outputRate, const string& file, const Folder& root) {
    Locker locker(lock);
    layers.clear();
    samples.clear();
    // parse sfz and map samples
    TextData s = readFile(file, root);
    Folder folder (section((file),'.',0,1), root); // Samples must be in a subfolder with the same name as the .sfz file
    Sample group, region; Sample* sample=&group;
    bool release=false;
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
            string value = s.untilAny(" \n\r"_);
            if(key=="sample"_) {
                String path = replace(replace(value,"\\"_,"/"_),".wav"_,".flac"_);
                sample->name = copy(path);
                sample->data = Map(path,folder);
                sample->flac = FLAC(sample->data);
                if(!rate) rate=sample->flac.rate;
                else if(rate!=sample->flac.rate) error("Sample rate mismatch",rate,sample->flac.rate);
            }
            else if(key=="trigger"_) { if(value=="release"_) sample->trigger = 1, release=true; else warn("unknown trigger",value); }
            else if(key=="lovel"_) sample->lovel=toInteger(value);
            else if(key=="hivel"_) sample->hivel=toInteger(value);
            else if(key=="lokey"_) sample->lokey=isInteger(value)?toInteger(value):noteToMIDI(value);
            else if(key=="hikey"_) sample->hikey=isInteger(value)?toInteger(value):noteToMIDI(value);
            else if(key=="pitch_keycenter"_) sample->pitch_keycenter=isInteger(value)?toInteger(value):noteToMIDI(value);
            else if(key=="key"_) sample->lokey=sample->hikey=sample->pitch_keycenter=isInteger(value)?toInteger(value):noteToMIDI(value);
            else if(key=="ampeg_release"_) {} /*sample->releaseTime=toDecimal(value);*/
            else if(key=="amp_veltrack"_) {} /*sample->amp_veltrack=toDecimal(value)/100;*/
            else if(key=="rt_decay"_) {}//sample->rt_decay=toInteger(value);
            else if(key=="volume"_) sample->volume = exp10(toDecimal(value)/20.0); // 20 to go from dB (energy) to amplitide
            else if(key=="tune"_) {} //FIXME
            else if(key=="ampeg_attack"_) {} //FIXME
            else if(key=="ampeg_vel2attack"_) {} //FIXME
            else if(key=="fil_type"_) {} //FIXME
            else if(key=="cutoff"_) {} //FIXME
            else error("Unknown opcode"_,key);
        }
    }

    for(Sample& s: samples) {
        if(release) {
            if(!existsFile(String(s.name+".env"_),folder)) {
                FLAC flac(s.data);
                array<float> envelope; uint size=0;
                while(flac.blockSize!=0) {
                    const uint period=1<<8;
                    while(flac.blockSize!=0 && size<period) { size+=flac.blockSize; flac.decodeFrame(); }
                    if(size>=period) {
                        envelope << sumOfSquares(flac, period);
                        flac.readIndex=(flac.readIndex+period)%flac.audio.capacity;
                        size-=period;
                    }
                }
                log(s.name+".env"_,envelope.size);
                writeFile(s.name+".env"_,cast<byte,float>(envelope),folder);
            }
            s.envelope = array<float>(cast<float,byte>(readFile(String(s.name+".env"_),folder)));
        }

        s.flac.decodeFrame(); // Decodes first frame of all samples to start mixing without latency
        s.data.lock(); // Locks compressed samples in memory

        for(int key: range(s.lokey,s.hikey+1)) { // Instantiates all pitch shifts on startup
            float shift = key-s.pitch_keycenter; //TODO: tune
            Layer* layer=0;
            for(Layer& l : layers) if(l.shift==shift) layer=&l;
            if(layer == 0) { // Generates pitch shifting (resampling) filter banks
                layers.grow(layers.size+1);
                layer = &layers.last();
                layer->shift = shift;
                layer->notes.reserve(256);
                if(shift || rate!=outputRate) {
                    const uint size = 2048; // Accurate frequency resolution while keeping reasonnable filter bank size
                    new (&layer->resampler) Resampler(2, size, round(size*exp2((-shift)/12.0)*outputRate/rate));
                }
            }
        }
    }

    array<byte> reverbFile = readFile("../reverb.flac"_,folder);
    FLAC reverbMedia(reverbFile);
    assert(reverbMedia.rate == rate);
    reverbSize = reverbMedia.duration;
    N = reverbSize+periodSize;
    buffer<float> stereoFilter(2*reverbSize,2*reverbSize,0.f);
    for(uint i=0;i<reverbSize;) {
        uint read = reverbMedia.read((float2*)(stereoFilter+2*i),min(1u<<16,reverbSize-i));
        i+=read;
    }

    // Computes normalization
    float sum=0; for(uint i: range(2*reverbSize)) sum += stereoFilter[i]*stereoFilter[i];
    const float scale = 1./sqrt(sum); // Normalizes

    // Reverses, scales and deinterleaves filter
    buffer<float> filter[2];
    for(int c=0;c<2;c++) filter[c] = buffer<float>(N,N,0.f);
    for(uint i: range(reverbSize)) for(int c=0;c<2;c++) filter[c][i] = scale*stereoFilter[2*i+c];

#if FFTW_THREADS
    fftwf_init_threads();
    fftwf_plan_with_nthreads(4);
#endif

    // Transforms reverb filter to frequency domain
    for(int c=0;c<2;c++) {
        reverbFilter[c] = buffer<float>(N,N,0.f);
        FFTW p (fftwf_plan_r2r_1d(N, filter[c], reverbFilter[c], FFTW_R2HC, FFTW_ESTIMATE));
        fftwf_execute(p);
        //for(uint i: range(N/2-N/4,N/2+N/4)) reverbFilter[c][i]=0; // Low-pass (some samples are aliased) //FIXME: only on those samples
    }

    // Allocates reverb buffer and temporary buffers
    input = buffer<float>(N);
    for(int c=0;c<2;c++) {
        reverbBuffer[c] = buffer<float>(N,N,0.f);
        forward[c] = fftwf_plan_r2r_1d(N, reverbBuffer[c], input, FFTW_R2HC, FFTW_ESTIMATE);
    }
    product = buffer<float>(N,N,0.f);
    backward = fftwf_plan_r2r_1d(N, product, input, FFTW_HC2R, FFTW_ESTIMATE);
}

/// Input events (realtime thread)

float Note::actualLevel(uint size) const {
    const int count = size>>8;
    if((flac.position>>8)+count>envelope.size) return 0; //fully decayed
    float sum=0; for(int i=0;i<count;i++) sum+=envelope[(flac.position>>8)+count]; //precomputed sum
    return sqrt(sum)/size;
}

void Sampler::noteEvent(uint key, uint velocity) {
    Note* current=0;
    if(velocity==0) {
        for(Layer& layer: layers) for(Note& note: layer.notes) if(note.key==key) {
            current=&note; //schedule release sample
            if(note.releaseTime) {//release fade out current note
                float step = pow(1.0/(1<<24),(/*2 samples/step*/2.0/(rate*note.releaseTime)));
                note.step=(v4sf){step,step,step,step};
                note.level=(v4sf){note.level[0],note.level[0],note.level[0]*step,note.level[0]*step}; // Pedantic
            }
        }
        if(!current) return; //already fully decayed
        velocity=current->velocity;
    }
    for(const Sample& s : samples) {
        if(s.trigger == (current?1:0) && s.lokey <= key && key <= s.hikey && s.lovel <= velocity && velocity <= s.hivel) {
            {Locker locker(lock);
                float shift = int(key)-s.pitch_keycenter; //TODO: tune
                Layer* layer=0;
                for(Layer& l : layers) if(l.shift==shift) layer=&l;
                assert_(layer && layer->notes.size<layer->notes.capacity);

                layer->notes.append( ::copy(s.flac) ); // Copies predecoded buffer and corresponding FLAC decoder state
                Note& note = layer->notes.last();
                note.envelope = s.envelope;
                if(!current) { // Press
                    note.key=key, note.velocity=velocity;
                    note.level = float4(s.volume * float(velocity) / 127.f); // E ~ A^2 ~ v^2 => A ~ v
                } else { // Release (rt_decay is unreliable, matching levels works better)
                    note.level = float4(min(8.f, current->level[0] * current->actualLevel(1<<14) / note.actualLevel(1<<11))); //341ms/21ms
                }
                if(note.level[0]<0x1p-23) { layer->notes.removeAt(layer->notes.size-1); return; }
                note.step=(v4sf){1,1,1,1};
                note.releaseTime=s.releaseTime;
                note.envelope=s.envelope;
                if(note.flac.sampleSize==16) note.level *= 0x1p8;
            }
            if(backgroundDecoder) queue(); //queue background decoder in main thread
            return;
        }
    }
    if(current) return; //release samples are not mandatory
    if(key<=30 || key>=90) return; // Harpsichord don't have the lowest/highest piano notes
    log("Missing sample"_,key);
    log(samples);
}

/// Background decoder (background thread)

void Sampler::event() { // Main thread event posted every period from Sampler::read by audio thread
    if(lock.tryLock()) { // Cleanup silent notes
        for(Layer& layer: layers) layer.notes.filter([](const Note& note){
            return (note.flac.blockSize==0 && note.readCount<int(2*periodSize)) || note.level[0]<0x1p-23;
        });
        lock.unlock();
    }
    for(;;) {
        Note* note=0; uint minBufferSize=-1;
        for(Layer& layer: layers) for(Note& n: layer.notes) { // Finds least buffered note
            if(n.flac.blockSize && n.writeCount>=(int)n.flac.blockSize && uint(n.flac.audio.size)<minBufferSize) {
                note=&n;
                minBufferSize=n.flac.audio.size;
            }
        }
        if(!note) { // All notes are already fully buffered
            break;
        }
        int size=note->flac.blockSize; note->writeCount.acquire(size); note->flac.decodeFrame(); note->readCount.release(size);
    }
    if(backgroundDecoder && time>lastTime) { int time=this->time; timeChanged(time-lastTime); lastTime=time; } // read MIDI file / update UI (while in main thread)
}

/// Audio mixer (realtime thread)
inline void mix(v4sf& level, v4sf step, v4sf* out, v4sf* in, uint size) {
    for(uint i: range(size)) { out[i] += level * in[i]; level *= step; }
}
void Note::read(v4sf* out, uint size) {
    if(flac.blockSize==0 && readCount<int(size*2)) { readCount.counter=0; return; } // end of stream
    if(!readCount.tryAcquire(size*2)) {
        log("decoder underrun", size*2, readCount.counter);
        readCount.acquire(size*2); //ensure decoder follows
    }
    uint beforeWrap = (flac.audio.capacity-flac.readIndex)/2;
    if(size>beforeWrap) {
        mix(level,step,out,(v4sf*)(flac.audio+flac.readIndex),beforeWrap);
        mix(level,step,out+beforeWrap,(v4sf*)(flac.audio+0),size-beforeWrap);
        flac.readIndex = (size-beforeWrap)*2;
    } else {
        mix(level,step,out,(v4sf*)(flac.audio+flac.readIndex),size);
        flac.readIndex += size*2;
    }
    flac.audio.size-=size*2; writeCount.release(size*2); //allow decoder to continue
    flac.position+=size*2; //keep track of position for release sample level matching
}

uint Sampler::read16(int16* output, uint size) { // Record
    v4sf buffer[size*2/4];
    read((float*)buffer, size);
    for(uint i: range(size*2/8)) ((v8hi*)output)[i] = packss( cvtps2dq(buffer[i*2+0]*0x1p-8f), cvtps2dq(buffer[i*2+1]*0x1p-8f) ); // 24bit -> 16bit with no headroom (saturate)
#if WAV
    if(record) record.write(ref<byte>((byte*)output,2*size*sizeof(int32)));
#endif
    return size;
}

uint Sampler::read(int32* output, uint size) { // Audio thread
    v4sf buffer[size*2/4];
    read((float*)buffer, size);
    for(uint i: range(size*2/4)) ((v4si*)output)[i] = cvtps2dq(buffer[i]*0x1p5f); // 24bit -> 32bit with 3bit headroom for multiple notes
#if WAV
    if(record) record.write(ref<byte>((byte*)output,2*size*sizeof(int32)));
#endif
    return size;
}

uint Sampler::read(float* output, uint size) {
    if(!backgroundDecoder) {
        event(); // Decodes before mixing
        for(Layer& layer: layers) for(Note& n: layer.notes) assert_(!n.flac.blockSize || n.readCount > align(2,layer.resampler.need(size)), n.readCount, align(2,layer.resampler.need(size)), n.flac.blockSize, n.writeCount, n.flac.audio.size);
    }
    clear(output, size*2);
    int noteCount = 0;
    {Locker locker(lock);
        for(Layer& layer: layers) { // Mixes all notes of all layers
            if(layer.resampler) {
                uint inSize=align(2,layer.resampler.need(size));
                if(layer.audio.capacity<inSize*2) layer.audio = buffer<float>(inSize*2);
                clear(layer.audio.begin(), inSize*2);
                for(Note& note: layer.notes) note.read((v4sf*)layer.audio.data, inSize/2);
                layer.resampler.filter<true>(layer.audio, inSize, output, size);
            } else {
                for(Note& note: layer.notes) note.read((v4sf*)output, size/2);
            }
            noteCount += layer.notes.size;
        }
    }
    if(noteCount==0 /*&& !record*/) { // Stops audio output when all notes are released
            if(!stopTime) stopTime=time; // First waits for reverb
            else if(time>stopTime+reverbSize) {
                stopTime=0;
                silence();
                //return 0; // Stops audio output (will be restarted on noteEvent (cf music.cc)) (FIXME: disable on video record)
            }
      } else stopTime=0;

    if(enableReverb) { // Convolution reverb
        if(size!=periodSize) error("Expected period size ",periodSize,"got",size);
        // Deinterleaves mixed signal into reverb buffer
        for(uint i: range(periodSize)) for(int c=0;c<2;c++) reverbBuffer[c][reverbSize+i] = output[2*i+c];

        for(int c=0;c<2;c++) {
            // Transforms reverb buffer to frequency-domain ( reverbBuffer -> input )
            fftwf_execute(forward[c]);

            for(uint i: range(reverbSize)) reverbBuffer[c][i] = reverbBuffer[c][i+periodSize]; // Shifts buffer for next frame (FIXME: ring buffer)

            // Complex multiplies input (reverb buffer) with kernel (reverb filter)
            const float* x = input;
            const float* y = reverbFilter[c];
            product[0u] = x[0] * y[0];
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
                output[2*i+c] = (1.f/N)*input[reverbSize+i];
            }
        }
    }

    time+=size;
    //frameSent(output, size);
    if(backgroundDecoder) queue(); // Queues background decoder in main thread
    else if(time>lastTime) { int time=this->time; timeChanged(time-lastTime); lastTime=time; } // read MIDI file / update UI (while in main thread)
    return size;
}

/*void Sampler::startRecord(const string& name) {
    String path = name+".wav"_;
    record = File(path,home(),Flags(WriteOnly|Create|Truncate));
    struct { char RIFF[4]={'R','I','F','F'}; int32 size; char WAVE[4]={'W','A','V','E'}; char fmt[4]={'f','m','t',' '};
             int32 headerSize=16; int16 compression=1; int16 channels=2; int32 rate=44100; int32 bps=rate*channels*sizeof(int32);
        int16 stride=channels*sizeof(int32); int16 bitdepth=32; char data[4]={'d','a','t','a'}; } packed header;
    record.write(raw(header));
    recordStart = time;
}
void Sampler::stopRecord() {
    if(record) { record.seek(4); record.write(raw<int32>(36+time)); record=0; }
}*/

Sampler::FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }
#if WAV
Sampler::~Sampler() { stopRecord(); }
#endif
constexpr uint Sampler::periodSize;
