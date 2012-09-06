#include "sampler.h"
#include "stream.h"
#include "file.h"
#include "flac.h"
#include "time.h"
#include "debug.h"
#include "linux.h"

double exp2(double x) { return __builtin_exp2(x); }
double exp10(double x) { return __builtin_exp(x*__builtin_log(10)); }

constexpr uint Sampler::period;

void Sampler::open(const ref<byte>& path) {
    // parse sfz and map samples
    Sample group;
    TextStream s(readFile(path));
    Folder folder = openFolder(section(path,'/',0,-2,true));
    Sample* sample=0;
    for(;;) {
        s.whileAny(" \n\r"_);
        if(!s) break;
        if(s.match("<group>"_)) { group=Sample(); sample = &group; }
        else if(s.match("<region>"_)) { assert_(!group.data.data); samples<<move(group); sample = &samples.last();  }
        else if(s.match("//"_)) {
            s.untilAny("\n\r"_);
            s.whileAny("\n\r"_);
        }
        else {
            ref<byte> key = s.until('=');
            ref<byte> value = s.untilAny(" \n\r"_);
            if(key=="sample"_) {
                ref<byte> path = replace(value,"\\"_,"/"_);
                sample->data = mapFile(path,folder);
                madvise((void*)sample->data.data, sample->data.size, MADV_SEQUENTIAL);
            }
            else if(key=="trigger"_) { if(value=="release"_) sample->trigger = 1, sample->release=0; }
            else if(key=="lovel"_) sample->lovel=toInteger(value);
            else if(key=="hivel"_) sample->hivel=toInteger(value);
            else if(key=="lokey"_) sample->lokey=toInteger(value);
            else if(key=="hikey"_) sample->hikey=toInteger(value);
            else if(key=="pitch_keycenter"_) sample->pitch_keycenter=toInteger(value);
            else if(key=="ampeg_release"_) sample->release=48000*toInteger(value);
            else if(key=="amp_veltrack"_) sample->amp_veltrack=toInteger(value);
            else if(key=="rt_decay"_) sample->rt_decay=toInteger(value);
            else if(key=="volume"_) sample->volume=toInteger(value);
            else error("Unknown opcode"_,key);
        }
    }

    //setup layers and pitch shifting
    for(int i=0;i<3;i++) { Layer& layer=layers[i];
        layer.size = round(period*exp2((i-1)/12.0));
        layer.buffer = allocate<float>(2*layer.size);
        if(layer.size!=period) new (&layer.resampler) Resampler(2, layer.size, period);
    }
}

#include "map.h"
uint availableMemory() {
    map<string, uint> info;
    for(TextStream s = ::readUpTo(openFile("/proc/meminfo"_),4096);s;) {
        ref<byte> key=s.until(':'); s.skip();
        uint value=toInteger(s.untilAny(" \n"_)); s.until('\n');
        info.insert(string(key), value);
    }
    return info.at(string("MemFree"_))+info.at(string("Inactive"_));
}

int K(int size) { return (size+4095)/4096*4; } //size rounded up to page in KiB
void Sampler::lock() {
    uint full=0; for(const Sample& s : samples) full += K(s.data.size);
    uint available = availableMemory();
    debug(log("Not locking in debug mode (",available/1024,"MiB available memory, full cache need",full/1024,"MiB)"); return;)
    if(full>available) {
        uint lock=0; for(const Sample& s : samples) lock += min(s.data.size/4096*4,available*1024/samples.size()/4096*4);
        log("Locking",lock/1024,"MiB of",available/1024,"MiB available memory, full cache need",full/1024,"MiB");
    }
    uint64 start=realTime();
    int i=0; for(const Sample& s : samples) {
        if(realTime()-start > 1000) { start=realTime(); log("Loading "_,i,"/",samples.size()); }
        uint size = s.data.size; if(full>available) size=min(size, available*1024/samples.size());
        mlock((void*)s.data.data, size);
        i++;
    }
}

void Sampler::queueEvent(int key, int velocity) { queue<<Event __(key,velocity); }
void Sampler::processEvent(Event e) { int key=e.key, velocity=e.velocity;
    int elapsed=0;
    if(velocity==0) {
        for(Note& note : active) if(note.release && note.key==key) {
            elapsed=note.time-note.remaining; velocity = note.velocity; //schedule release sample
            note.remaining = min(note.remaining,note.release); //schedule active sample fade out
        }
        if(!elapsed) return; //already fully decayed
    }
    for(const Sample& s : samples) {
        if(!!elapsed == s.trigger && key >= s.lokey && key <= s.hikey && velocity >= s.lovel && velocity <= s.hivel) {
            float level=1;
            if(elapsed) level=exp10((s.volume-3*(elapsed/48000.0))/20.0);
            else level = (1-(s.amp_veltrack/100.0*(1-(velocity*velocity)/(127.0*127.0)))) * exp10(s.volume/20.0);
            if(level<1/256.0) return;
            active.grow(active.size()+1);
            Note& note = active.last();
            note.start(s.data);
            note.remaining=note.time;
            note.release=s.release?4096:0; //TODO: reverb
            note.key=key;
            int shift = key-s.pitch_keycenter; assert(shift>=-1 && shift<=1,"unsupported pitch shift"_,shift);
            note.layer=1+shift;
            note.velocity=velocity;
            note.level=level;
            return;
        }
    }
    if(!elapsed) error("Missing sample"_,key,velocity);
}

void Sampler::read(int16 *output, uint unused size) {
    assert(size==period,"Sampler supports only fixed size period of", period,"frames, trying to read",size,"frames");
    if(queue) processEvent(queue.take(0));
    timeChanged(time);
    for(Layer& layer : layers) layer.active=false;
    for(uint i=0;i<active.size();i++) { Note& n = active[i];
        Layer& layer = layers[n.layer];
        int frame = layer.size;
        if(!layer.active) { clear(layer.buffer, 2*layer.size); layer.active = true; } //clear if first note in layer
        if(!n.release || n.remaining >= n.release+frame) { //sustain
            float* out = layer.buffer; assert(out);
            while(frame > n.blockSize && n.remaining > n.blockSize) {
                int* in = (int*)(n.buffer+n.position);
                for(int i=0; i<2*n.blockSize; i++) out[i] += n.level * float(in[i]);
                out += 2*n.blockSize; n.remaining -= n.blockSize; frame -= n.blockSize;
                n.readFrame();
            }
            int* in = (int*)(n.buffer+n.position); assert(in);
            for(int i=0; i<2*frame && n.remaining>=0; i++, n.remaining--)  out[i] += n.level * float(in[i]);
            n.position += frame;
            n.blockSize -= frame;
        } else { //release
            float reciprocal = n.level / n.release;
            float* out = layer.buffer;
            while(frame > n.blockSize && n.remaining > n.blockSize) {
                int* in = (int*)(n.buffer+n.position);
                for(int i=0; i<2*n.blockSize && n.remaining>=0; i++,n.remaining--) out[i] += min(n.release,n.remaining)*reciprocal * float(in[i]);
                out += 2*n.blockSize; frame -= n.blockSize;
                n.readFrame();
            }
            int* in = (int*)(n.buffer+n.position);
            for(int i=0; i<2*frame && n.remaining>=0; i++,n.remaining--) out[i] += min(n.release,n.remaining)*reciprocal * float(in[i]);
            n.position += frame;
            n.blockSize -= frame;
        }
        if(n.remaining <= 0) { active.removeAt(i); i--; continue; } //released
    }
    bool anyActive = layers[1].active;
    for(int i=0;i<=2;i+=2) { if(!layers[i].active) continue;
        int in = layers[i].size, out = period;
        layers[i].resampler.filter(layers[i].buffer,&in, layers[1].buffer, &out, anyActive);
        anyActive=true; //directly mix if unresampled layer or any previous layer is active
    }
    if(anyActive) {
        float* in = layers[1].buffer;
        uint clip=0;
        for(uint i=0;i<period*2;i++) { int o=int(in[i])>>9; if(o<-32768 || o>32767) clip++; output[i] = ::clip(-32768,o,32767);} //>>9 to play at least 4 attacks without clipping
        if(clip>64) log("clip",clip);
    } else clear(output,period*2); //no active note -> play silence (TODO: pcm_stop)
    if(record) write(record,ref<byte>((byte*)output,period*2*sizeof(int16)));
    time+=period;
}

void Sampler::recordWAV(const string& path) {
    record = move(createFile(path));
    struct { char RIFF[4]={'R','I','F','F'}; int32 size; char WAVE[4]={'W','A','V','E'}; char fmt[4]={'f','m','t',' '};
        int32 headerSize=16; int16 compression=1; int16 channels=2; int32 rate=48000; int32 bps=48000*4;
        int16 stride=4; int16 bitdepth=16; char data[4]={'d','a','t','a'}; /*size?*/ } packed header;
    write(record,raw(header));
}
Sampler::~Sampler() {
    if(!record) return;
    error("Recording unsupported");
    //lseek(record,4,SEEK_SET); write(record,raw<int32>(36+time)); lseek(record,0,SEEK_END); close(record);
}
