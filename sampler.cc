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
    full=0; for(const Sample& s : samples) full += K(s.data.size);
    available = availableMemory();
    debug(log("Not locking in debug mode (",available/1024,"MiB available memory, full cache need",full/1024,"MiB)"); return;)
    if(full>available) {
        uint lock=0; for(const Sample& s : samples) lock += min(s.data.size/4096*4,available*1024/samples.size()/4096*4);
        log("Locking",lock/1024,"MiB of",available/1024,"MiB available memory, full cache need",full/1024,"MiB");
    }
    current=0; wait();
}
void Sampler::event() {
    const Sample& s=samples[current++];
    uint size = s.data.size;
    if(full>available) size=min(size, available*1024/samples.size());
    mlock((void*)s.data.data, size);
    progressChanged(current,samples.size());
    if(current<samples.size()) wait();
}

void Sampler::queueEvent(int key, int velocity) { queue<<Event __(key,velocity); }
void Sampler::processEvent(Event e) { int key=e.key, velocity=e.velocity;
    int elapsed=0;
    if(velocity==0) {
        for(Note& note : active) if(note.release && note.key==key) {
            elapsed=note.duration-note.remaining; velocity = note.velocity; //schedule release sample
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
            Note note;
            note.start(s.data);
            note.remaining=note.duration;
            note.release=s.release?4096:0; //TODO: reverb
            note.key=key;
            int shift = key-s.pitch_keycenter; assert(shift>=-1 && shift<=1,"unsupported pitch shift"_,shift);
            note.layer=1+shift;
            note.velocity=velocity;
            note.level=level;
            active << move(note);
        }
    }
    if(!elapsed) error("Missing sample"_,key,velocity);
}

template<bool mix> void Note::read(float* out, uint size) {
#define mix(expr) ({float s=expr; if(mix) out[0]+=s; else out[0]=s; })
    if(!release || remaining >= release+size) { //sustain
        while(size > blockSize && remaining > blockSize) {
            for(float* end=out+2*blockSize; out<end; block++, out++) mix(level * block[0]);
            remaining -= blockSize;
            size -= blockSize;
            readBlock(); block=buffer;
        }
        float* end=out+2*size;
        if(remaining<size) size=remaining;
        for(float* end=out+2*size; out<end; block++, out++) mix(level * block[0]);
        remaining -= size;
        if(!mix) for(;out<end;out++) out[0]=0;
        blockSize -= size;
    } else { //release
        float reciprocal = level / release;
        while(size > blockSize && remaining > blockSize) {
            for(float* end=out+2*blockSize; out<end; remaining--, block++, out++) mix(min(release,remaining)*reciprocal * block[0]);
            size -= blockSize;
            readBlock(); block=buffer;
        }
        float* end=out+2*size;
        if(remaining<size) size=remaining;
        for(float* end=out+2*size; out<end; remaining--, block++, out++) mix(min(release,remaining)*reciprocal * block[0]);
        if(!mix) for(;out<end;out++) out[0]=0;
        blockSize -= size;
    }
}

bool Sampler::read(int16 *output, uint unused size) {
    assert(size==period,"Sampler supports only fixed size period of", period,"frames, trying to read",size,"frames");
    if(queue) processEvent(queue.take(0));
    timeChanged(time);
    for(Layer& layer : layers) layer.active=false;
    for(uint i=0;i<active.size();) { Note& n = active[i];
        Layer& layer = layers[n.layer];
        if(layer.active) n.read<true>(layer.buffer,layer.size);
        else n.read<false>(layer.buffer,layer.size), layer.active = true;
        if(n.remaining <= 0) active.removeAt(i); else i++; //cleanup released or decayed notes
    }
    bool anyActive = layers[1].active;
    for(int i=0;i<=2;i+=2) { if(!layers[i].active) continue;
        if(anyActive) layers[i].resampler.filter<true>(layers[i].buffer, layers[i].size, layers[1].buffer, period);
        else layers[i].resampler.filter<false>(layers[i].buffer, layers[i].size, layers[1].buffer, period);
        anyActive=true; //directly mix if unresampled layer or any previous layer is active
    }
    if(anyActive) {
        float* in = layers[1].buffer;
        uint clip=0;
        for(uint i=0;i<period*2;i++) { int o=int(in[i])>>9; if(o<-32768 || o>32767) clip++; output[i] = ::clip(-32768,o,32767);} //>>9 to play at least 4 attacks without clipping
        if(clip>64) log("clip",clip);
    } else { if(record || true/*TODO: noteEvent->audio.start*/) clear(output,period*2); else return false; }
    if(record) write(record,ref<byte>((byte*)output,period*2*sizeof(int16)));
    time+=period;
    return true;
}

void Sampler::recordWAV(const ref<byte>& path) {
    error("Recording unsupported");
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
