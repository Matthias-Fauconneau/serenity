#include "simd.h"
#include "sampler.h"
#include "resample.h"
#include "data.h"
#include "file.h"
#include "flac.h"
#include "time.h"
#include "string.h"
#include "math.h"
#if REVERB
#include <fftw3.h> //fftw3f
#define FFTW_THREADS 1
#if FFTW_THREADS
#include <fftw3.h> //fftw3f_threads
#endif
#endif

struct Sampler::Sample {
	String name;
	Map data; FLAC flac; array<float> envelope; //Sample data
	int trigger=0; uint lovel=0; uint hivel=127; uint lokey=0; uint hikey=127; //Input controls
	int pitch_keycenter=60; float releaseTime=/*0*/1; float amp_veltrack=1; float volume=1; //Performance parameters
};
inline String str(const  Sampler::Sample& s) { return str(s.lokey)+"-"_+str(s.pitch_keycenter)+"-"_+str(s.hikey); }
inline bool operator <=(const  Sampler::Sample& a, const Sampler::Sample& b) { return a.pitch_keycenter<=b.pitch_keycenter; }

struct Note {
	default_move(Note);
	explicit Note(FLAC&& flac):flac(move(flac)),readCount(this->flac.audio.size),writeCount((int)this->flac.audio.capacity-this->flac.audio.size){}
	FLAC flac;
	v4sf level; //current note attenuation
	v4sf step; //coefficient for release fade out = (2 ** -24)**(1/releaseTime)
	Semaphore readCount; //decoder thread releases decoded samples, audio thread acquires
	Semaphore writeCount; //audio thread release free samples, decoder thread acquires
	uint releaseTime; //to compute step
	uint key=0, velocity=0; //to match release sample
	ref<float> envelope; //to level release sample
	/// Decodes frames until \a available samples is over \a need
	void decode(uint need);
	/// Reads samples to be mixed into \a output
	void read(const mref<float2>& output);
	/// Computes sum of squares on the next \a size samples (to compute envelope)
	float sumOfSquares(uint size);
	/// Computes actual sound level on the next \a size samples (using precomputed envelope)
	float actualLevel(uint size) const;
};

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

Sampler::Sampler(uint outputRate, string path, function<void(uint)> timeChanged) : timeChanged(timeChanged) {
    Locker locker(lock);
    layers.clear();
    samples.clear();
    // parse sfz and map samples
	TextData s = readFile(path);
	Folder folder = section(path,'.',0,1); // Samples must be in a subfolder with the same name as the .sfz file
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
            s.whileAny(" "_);
            string value = s.untilAny(" \n\r"_);
            if(key=="sample"_) {
                String path = replace(replace(value,"\\"_,"/"_),".wav"_,".flac"_);
                sample->name = copy(path);
                sample->data = Map(path,folder);
                sample->flac = FLAC(sample->data);
                if(!rate) rate=sample->flac.rate;
                else if(rate!=sample->flac.rate) error("Sample rate mismatch",rate,sample->flac.rate);
            }
            else if(key=="trigger"_) { if(value=="release"_) sample->trigger = 1, release=true; else error("unknown trigger",value); }
			else if(key=="lovel"_) sample->lovel=parseInteger(value);
			else if(key=="hivel"_) sample->hivel=parseInteger(value);
			else if(key=="lokey"_) sample->lokey=isInteger(value)?parseInteger(value):noteToMIDI(value);
			else if(key=="hikey"_) sample->hikey=isInteger(value)?parseInteger(value):noteToMIDI(value);
			else if(key=="pitch_keycenter"_) sample->pitch_keycenter=isInteger(value)?parseInteger(value):noteToMIDI(value);
			else if(key=="key"_) sample->lokey=sample->hikey=sample->pitch_keycenter=isInteger(value)?parseInteger(value):noteToMIDI(value);
            else if(key=="ampeg_release"_) {} /*sample->releaseTime=fromDecimal(value);*/
            else if(key=="amp_veltrack"_) {} /*sample->amp_veltrack=fromDecimal(value)/100;*/
            else if(key=="rt_decay"_) {}//sample->rt_decay=toInteger(value);
			else if(key=="volume"_) sample->volume = exp10(parseDecimal(value)/20.0); // 20 to go from dB (energy) to amplitide
            else if(key=="tune"_) {} //FIXME
            else if(key=="ampeg_attack"_) {} //FIXME
            else if(key=="ampeg_vel2attack"_) {} //FIXME
            else if(key=="fil_type"_) {} //FIXME
            else if(key=="cutoff"_) {} //FIXME
            else error("Unknown opcode"_,key);
        }
    }
    if(sample==&region) samples.insertSorted(move(region)); // Do not forget to record last region

    if(release) Folder("Envelope"_, folder, true);
    for(Sample& s: samples) {
        if(release) {
            if(!existsFile(String("Envelope/"_+s.name+".env"_),folder)) {
                FLAC flac(s.data);
                array<float> envelope; uint size=0;
                while(flac.blockSize!=0) {
                    const uint period=1<<8;
                    while(flac.blockSize!=0 && size<period) { size+=flac.blockSize; flac.decodeFrame(); }
                    if(size>=period) {
						envelope.append( sumOfSquares(flac, period) );
                        flac.readIndex=(flac.readIndex+period)%flac.audio.capacity;
                        size-=period;
                    }
                }
                log(s.name, envelope.size);
                writeFile("Envelope/"_+s.name+".env"_,cast<byte>(envelope),folder);
            }
            s.envelope = cast<float>(readFile(String("Envelope/"_+s.name+".env"_),folder));
        }

        s.flac.decodeFrame(); // Decodes first frame of all samples to start mixing without latency
		//s.data.lock(); // Locks compressed samples in memory

        for(int key: range(s.lokey,s.hikey+1)) { // Instantiates all pitch shifts on startup
            float shift = key-s.pitch_keycenter; //TODO: tune
            Layer* layer=0;
            for(Layer& l : layers) if(l.shift==shift) layer=&l;
            if(layer == 0) { // Generates pitch shifting (resampling) filter banks
				Layer layer;
				layer.shift = shift;
				//layer->notes.reserve(256);
                if(shift || rate!=outputRate) {
                    const uint size = 2048; // Accurate frequency resolution while keeping reasonnable filter bank size
					layer.resampler = Resampler(2, size, round(size*exp2((-shift)/12.0)*outputRate/rate));
                }
				layers.append(move(layer));
            }
        }
    }

#if REVERB
    Audio reverb = decodeAudio(readFile("../reverb.flac"_,folder));
    assert(reverb.rate == rate);
    //reverbSize = reverb.size;
    N = reverb.size+periodSize;

    // Computes normalization
    float sum=0; for(uint i: range(reverb.size)) sum += reverb[i][0]*reverb[i][0] + reverb[i][1]*reverb[i][1];
    const float scale = 1./sqrt(sum); // Normalizes

    // Reverses, scales and deinterleaves filter
    buffer<float> filter[2] = {buffer<float>(N,N,0),  buffer<float>(N,N,0)};
    for(uint i: range(reverb.size)) for(int c=0;c<2;c++) filter[c][i] = scale*reverb[i][c];

#if FFTW_THREADS
    fftwf_init_threads();
    fftwf_plan_with_nthreads(4);
#endif

    // Transforms reverb filter to frequency domain
    for(int c=0;c<2;c++) {
        reverbFilter[c] = buffer<float>(N);
        FFTW p (fftwf_plan_r2r_1d(N, filter[c].begin(), reverbFilter[c].begin(), FFTW_R2HC, FFTW_ESTIMATE));
        fftwf_execute(p);
    }

    // Allocates reverb buffer and temporary buffers
    input = buffer<float>(N);
    for(int c=0;c<2;c++) {
        reverbBuffer[c] = buffer<float>(N,N,0.f);
        forward[c] = fftwf_plan_r2r_1d(N, reverbBuffer[c].begin(), input.begin(), FFTW_R2HC, FFTW_ESTIMATE);
    }
    product = buffer<float>(N,N,0.f);
    backward = fftwf_plan_r2r_1d(N, product.begin(), input.begin(), FFTW_HC2R, FFTW_ESTIMATE);
#endif
}

/// Input events (realtime thread)

float Note::actualLevel(uint size) const {
    const int count = size>>8;
    if((flac.position>>8)+count>envelope.size) return 0; //fully decayed
    float sum=0; for(int i=0;i<count;i++) sum+=envelope[(flac.position>>8)+count]; //precomputed sum
    return sqrt(sum)/size;
}

void Sampler::noteEvent(uint key, uint velocity) {
    Note* released=0;
    if(velocity==0) {
        for(Layer& layer: layers) for(Note& note: layer.notes) if(note.key==key) {
            released=&note; // Records released note to match release sample level
            if(note.releaseTime) { // Release fades out current note
                float step = pow(1.0/(1<<24),(2./*2 frame/step*//(rate*note.releaseTime)));
                note.step=(v4sf){step,step,step,step};
                note.level[2]=note.level[3]=note.level[0]*step/2; // Steps level of second frame
            }
        }
        if(!released) return; // Already fully decayed
        velocity=released->velocity;
    }
    for(const Sample& s : samples) {
        if(s.trigger == (released?1:0) && s.lokey <= key && key <= s.hikey && s.lovel <= velocity && velocity <= s.hivel) {
            {Locker locker(lock);
                float shift = int(key)-s.pitch_keycenter; //TODO: tune
                Layer* layer=0;
                for(Layer& l : layers) if(l.shift==shift) layer=&l;
                assert(layer);
                if(layer->notes.size>=layer->notes.capacity) log(layer->notes.size, layer->notes.capacity);

				layer->notes.append(Note(::copy(s.flac))); // Copies predecoded buffer and corresponding FLAC decoder state
                Note& note = layer->notes.last();
                note.envelope = s.envelope;
                if(!released) { // Press
                    note.key=key, note.velocity=velocity;
                    note.level = float4(s.volume * float(velocity) / 127.f); // E ~ A^2 ~ v^2 => A ~ v
                } else { // Release (rt_decay is unreliable, matching levels works better), FIXME: window length for energy evaluation is arbitrary
                    note.level = float4(min(8.f, released->level[0] * released->actualLevel(1<<14) / note.actualLevel(1<<11))); // 341ms/21ms
                }
                if(note.level[0]<0x1p-15) { layer->notes.removeAt(layer->notes.size-1); return; }
                note.step=(v4sf){1,1,1,1};
                note.releaseTime=s.releaseTime;
                note.envelope=s.envelope;
                if(note.flac.sampleSize==16) note.level *= float4(0x1p8f);
            }
            if(backgroundDecoder) queue(); //queue background decoder in main thread
            return;
        }
    }
    if(released) return; // Release samples are not mandatory
    if(key<=30 || key>=90) return; // Some instruments have a narrower range
    log("Missing sample"_, key);
}

/// Background decoder (background thread)

void Sampler::event() { // Main thread event posted every period from Sampler::read by audio thread
	if(backgroundDecoder) timeChanged(time); // Read MIDI file / updates UI (while in main thread)
	if(lock.tryLock()) { // Cleanups silent notes
        for(Layer& layer: layers) layer.notes.filter([](const Note& note){
            return (note.flac.blockSize==0 && note.readCount<int(2*periodSize)) || note.level[0]<0x1p-15;
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
}

/// Audio mixer (realtime thread)
inline void mix(v4sf& level, v4sf step, const mref<float2>& output, const mref<float2>& input) {
    assert(output.size == input.size);
    uint size = output.size;
    v4sf* out = (v4sf*)output.data;
    v4sf* in = (v4sf*)input.data;
    for(uint i: range(size/2)) { out[i] += level * in[i]; level *= step; }
}
void Note::read(const mref<float2>& output) {
    if(flac.blockSize==0 && readCount<int(output.size)) { readCount.counter=0; return; } // end of stream
    if(!readCount.tryAcquire(output.size)) {
		log("Decoder underrun", output.size, readCount.counter);
		readCount.acquire(output.size); // Ensures decoder follows
    }
    uint beforeWrap = flac.audio.capacity-flac.readIndex;
    if(output.size>beforeWrap) {
        mix(level,step, output.slice(0, beforeWrap), flac.audio.slice(flac.readIndex,beforeWrap));
        mix(level,step, output.slice(beforeWrap), flac.audio.slice(0,output.size-beforeWrap));
        flac.readIndex = output.size-beforeWrap;
    } else {
        mix(level,step,output, flac.audio.slice(flac.readIndex,output.size));
        flac.readIndex += output.size;
    }
	flac.audio.size -= output.size; writeCount.release(output.size); // Allows decoder to continue
	flac.position += output.size; // Keeps track of position for release sample level matching
}

uint Sampler::read(const mref<int2>& output) { // Audio thread
    v4sf buffer[output.size*2/4];
    uint size = read(mref<float2>((float2*)buffer, output.size));
    for(uint i: range(size*2/4)) ((v4si*)output.data)[i] = cvtps2dq(buffer[i]*float4(0x1p5f)); // 24bit -> 32bit with 3bit headroom for multiple notes
    return size;
}

uint Sampler::read(const mref<float2>& output) {
    if(!backgroundDecoder) {
        event(); // Decodes before mixing
        //for(Layer& layer: layers) for(Note& n: layer.notes) assert_(!n.flac.blockSize || n.readCount > (int)align(2,layer.resampler.need(output.size)));
    }
    output.clear(0);
    int noteCount = 0;
    {Locker locker(lock);
        for(Layer& layer: layers) { // Mixes all notes of all layers
            if(layer.resampler) {
                /*int need = layer.resampler.need(output.size);
                const auto& o = layer.resampler;
                assert(need >= 0, need, output.size,
                       o.integerIndex,
                       output.size*o.integerAdvance+int(o.fractionalIndex+output.size*o.fractionalAdvance+o.targetRate-1)/o.targetRate,
                       o.writeIndex );*/
                int need = layer.resampler.need(output.size);
                if(need >= 0 ) {
                    uint inSize = align(2, need);
                    if(layer.audio.capacity<inSize) layer.audio = buffer<float2>(inSize);
                    layer.audio.clear(0);
                    for(Note& note: layer.notes) note.read(layer.audio);
                    layer.resampler.write(layer.audio);
                }
                layer.resampler.read<true>(output);
            } else {
                for(Note& note: layer.notes) note.read(output);
            }
            noteCount += layer.notes.size;
        }
    }
    if(noteCount==0 /*&& !record*/) { // Stops audio output when all notes are released
            if(!stopTime) stopTime=time; // First waits for reverb
            else if(time>stopTime+N) {
                stopTime=0;
                silence();
                //return 0; // Stops audio output (will be restarted on noteEvent (cf music.cc)) (FIXME: disable on video record)
            }
      } else stopTime=0;

#if REVERB
    if(enableReverb) { // Convolution reverb
        if(output.size!=periodSize) error("Expected period size ",periodSize,"got",output.size);
        // Deinterleaves mixed signal into reverb buffer
        for(uint i: range(periodSize)) for(int c=0;c<2;c++) reverbBuffer[c][N-periodSize+i] = output[i][c];

        for(int c=0;c<2;c++) {
            // Transforms reverb buffer to frequency-domain ( reverbBuffer -> input )
            fftwf_execute(forward[c]);

            for(uint i: range(N-periodSize)) reverbBuffer[c][i] = reverbBuffer[c][i+periodSize]; // Shifts buffer for next frame (FIXME: ring buffer)

            // Complex multiplies input (reverb buffer) with kernel (reverb filter)
            const ref<float>& A = input;
            const ref<float>& B = reverbFilter[c];
            product[0u] = A[0] * B[0];
            for(uint j = 1; j < N/2; j++) {
                float a = A[j];
                float b = A[N - j];
                float c = B[j];
                float d = B[N - j];
                product[j] = a*c-b*d;
                product[N - j] = a*d+b*c;
            }
            product[N/2] = A[N/2] * B[N/2];

            // Transforms product back to time-domain ( product -> input )
            fftwf_execute(backward);

            for(uint i: range(periodSize)) { // Normalizes and writes samples back in output buffer
                output[i][c] = (1.f/N)*input[N-periodSize+i];
            }
        }
    }
#endif

    time += output.size;
    if(backgroundDecoder) queue(); // Queues background decoder in main thread
    else timeChanged(time); // read MIDI file / update UI
    return output.size;
}

#if REVERB
Sampler::FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }
#endif
constexpr uint Sampler::periodSize;
Sampler::~Sampler() {}
