#include "sampler.h"
#include "data.h"
#include "file.h"
#include "flac.h"
#include "time.h"
#include "debug.h"

double exp2(double x) { return __builtin_exp2(x); }
double log2(double x) { return __builtin_log2(x); }
double exp10(double x) { return exp2(x*log2(10)); }
double log10(double x) { return log2(x)/log2(10); }
double dB(double x) { return 10*log10(x); }
#define pow __builtin_pow

template<bool mix> bool Note::read(float2* out, uint size) {
    float2* blockIndex=this->blockIndex; float2 *blockEnd=this->blockEnd; float2* frameEnd=blockIndex+size; float2* releaseStart=blockIndex+this->releaseStart-position;
    for(;;) {
        float2* end=min(frameEnd,blockEnd);
        if(end>=releaseStart) { //release
            float2 level=this->level, step=this->step;
            if(mix) for(;blockIndex<end; blockIndex++, out++, level*=step) out[0]+=level*blockIndex[0];
            else for(;blockIndex<end; blockIndex++, out++, level*=step) out[0]=level*blockIndex[0];
            this->level=level;
            if(level[0]<=1.0f/(1<<24)) return false;
        } else { //sustain
            end = min(end, releaseStart);
            if(mix) for(;blockIndex<end; blockIndex++, out++) out[0]+=level*blockIndex[0];
            else for(;blockIndex<end; blockIndex++, out++) out[0]=level*blockIndex[0];
        }
        if(blockIndex==frameEnd) break;
        else if(blockIndex==blockEnd) {
            position+=blockSize;
            if(position==duration) {
                if(!mix) for(float2* end=out+ptr(frameEnd-blockIndex); out<end; out++) out[0]=__(0,0);
                return false;
            }
            size=frameEnd-blockEnd; readFrame(); blockIndex=this->blockIndex; blockEnd=this->blockEnd;
            frameEnd=blockIndex+size; releaseStart=blockIndex+this->releaseStart-position;
        }
    }
    this->blockIndex=blockIndex; this->blockEnd=blockEnd;
    return true;
}
float Note::rootMeanSquare(uint size) {
    // Saves stream state
    uint unread=blockEnd-blockIndex; float2 buffer[unread];
    uint index=this->index, blockSize=this->blockSize; float2* blockIndex=this->blockIndex, *blockEnd=this->blockEnd; copy(buffer,blockIndex,unread);

    float2 sum={0,0};
    for(uint i=0;i<size;) {
        for(float2* in=this->buffer; i<size && in<this->buffer+this->blockSize; i++, in++) sum += (*in) * (*in);
        if(i<size) readFrame();
    }
    float level = sqrt(sum[0]+sum[1])/size/(1<<24);

    // Restores stream state
    this->index=index, this->blockSize=blockSize, this->blockIndex=blockIndex, this->blockEnd=blockEnd; copy(blockIndex,buffer,unread);
    return level;
}

constexpr uint Sampler::period;

void Sampler::open(const ref<byte>& path) {
    // parse sfz and map samples
    Sample group;
    TextData s = readFile(path);
    Folder folder = section(path,'/',0,-2,true);
    Sample* sample=0;
    for(;;) {
        s.whileAny(" \n\r"_);
        if(!s) break;
        if(s.match("<group>"_)) { group=Sample(); sample = &group; }
        else if(s.match("<region>"_)) { assert_(!group.map.data); samples<<move(group); sample = &samples.last();  }
        else if(s.match("//"_)) {
            s.untilAny("\n\r"_);
            s.whileAny("\n\r"_);
        }
        else {
            ref<byte> key = s.until('=');
            ref<byte> value = s.untilAny(" \n\r"_);
            if(key=="sample"_) {
                ref<byte> path = replace(value,"\\"_,"/"_);
                sample->map = Map(path,folder);
            }
            else if(key=="trigger"_) { if(value=="release"_) sample->trigger = 1; else warn("unknown trigger",value); }
            else if(key=="lovel"_) sample->lovel=toInteger(value);
            else if(key=="hivel"_) sample->hivel=toInteger(value);
            else if(key=="lokey"_) sample->lokey=toInteger(value);
            else if(key=="hikey"_) sample->hikey=toInteger(value);
            else if(key=="pitch_keycenter"_) sample->pitch_keycenter=toInteger(value);
            else if(key=="ampeg_release"_) sample->releaseTime=48000*toInteger(value);
            else if(key=="amp_veltrack"_) sample->amp_veltrack=toInteger(value);
            else if(key=="rt_decay"_) sample->rt_decay=toInteger(value);
            else if(key=="volume"_) sample->volume=toInteger(value);
            else error("Unknown opcode"_,key);
        }
    }

    //setup layers and pitch shifting
    for(int i=0;i<3;i++) { Layer& layer=layers[i];
        layer.size = round(period*exp2((i-1)/12.0));
        layer.buffer = allocate<float2>(layer.size);
        if(layer.size!=period) new (&layer.resampler) Resampler(2, layer.size, period);
    }
}

#include "map.h"
uint availableMemory() {
    map<string, uint> info;
    for(TextData s = File("/proc/meminfo"_).readUpTo(4096);s;) {
        ref<byte> key=s.until(':'); s.skip();
        uint value=toInteger(s.untilAny(" \n"_)); s.until('\n');
        info.insert(string(key), value);
    }
    return info.at(string("MemFree"_))+info.at(string("Inactive"_));
}

int K(int size) { return (size+4095)/4096*4; } //size rounded up to page in KiB
void Sampler::lock() {
    full=0; for(const Sample& s : samples) full += K(s.map.size);
    debug(log("Not locking in debug mode (",full/1024,"MiB)"); return;)
    available = availableMemory();
    if(full>available) {
        uint lock=0; for(const Sample& s : samples) lock += min(s.map.size/4096*4,available*1024/samples.size()/4096*4);
        log("Locking",lock/1024,"MiB of",available/1024,"MiB available memory, full cache need",full/1024,"MiB");
    }
    current=0; wait();
}
void Sampler::event() {
    const Sample& s=samples[current++];
    uint size = s.map.size;
    if(full>available) size=min(size, available*1024/samples.size());
    s.map.lock(size);
    progressChanged(current,samples.size());
    if(current<samples.size()) wait();
}

void Sampler::queueEvent(int key, int velocity) { queue<<Event __(key,velocity); }
void Sampler::processEvent(Event e) { int key=e.key, velocity=e.velocity;
    Note* current=0;
    if(velocity==0) {
        for(Note& note : active) if(note.key==key) {
            current=&note; //schedule release sample
            note.releaseStart = note.position; //schedule active sample fade out
        }
        if(!current) return; //already fully decayed
    }
    for(const Sample& s : samples) {
        if(s.trigger == (current?1:0) && s.lokey <= key && key <= s.hikey && s.lovel <= velocity && velocity <= s.hivel) {
            float level = (1-(s.amp_veltrack/100.0*(1-(velocity*velocity)/(127.0*127.0)))) * exp10(s.volume/20.0);
            active << Note(s.map);
            Note& note = active.last();
            note.layer=1+key-s.pitch_keycenter; assert_(note.layer<3);
            if(!current) note.key=key;
            else { //rt_decay is unreliable, matching levels works better
                level=current->rootMeanSquare(32768)/note.rootMeanSquare(2048);
                level *= current->level[0];
                if(level>8) level=8;
            }
            note.level=__(level,level);
            note.releaseStart=note.duration;
            if(s.releaseTime) {
                float step = pow(1.0/(1<<24),(1.0/s.releaseTime));
                note.step=__(step,step);
            }
            return;
        }
    }
    if(current) return; //release samples are not mandatory
    error("Missing sample"_,key,velocity);
}

bool Sampler::read(int16 *output, uint unused size) {
    assert_(size==period);
    if(queue) processEvent(queue.take(0));
    timeChanged(time);
    for(Layer& layer : layers) layer.active=false;
    for(uint i=0;i<active.size();) { Note& n = active[i];
        Layer& layer = layers[n.layer];
        if(!(layer.active?n.read<true>(layer.buffer,layer.size):n.read<false>(layer.buffer,layer.size))) active.removeAt(i); else i++;
        layer.active = true;
    }
    bool anyActive = layers[1].active;
    for(int i=0;i<=2;i+=2) { if(!layers[i].active) continue;
        if(anyActive) layers[i].resampler.filter<true>((float*)layers[i].buffer, layers[i].size, (float*)layers[1].buffer, period);
        else layers[i].resampler.filter<false>((float*)layers[i].buffer, layers[i].size, (float*)layers[1].buffer, period);
        anyActive=true; //directly mix if unresampled layer or any previous layer is active
    }
    if(anyActive) {
        float* in = (float*)layers[1].buffer;
        uint clip=0;
        for(uint i=0;i<period*2;i++) {
            float f = in[i];
            int o=int(f)>>9; //2x 24bit -> 16bit
            if(o<-32768 || o>32767) clip++; output[i] = ::clip(-32768,o,32767);}
        if(clip>64) log("clip",clip);
    } else { if(record || true/*TODO: noteEvent->audio.start*/) clear(output,period*2); else return false; }
    if(record) record.write(ref<byte>((byte*)output,period*2*sizeof(int16)));
    time+=period;
    return true;
}

void Sampler::recordWAV(const ref<byte>& path) {
    error("Recording unsupported");
    record = File(path,root(),File::WriteOnly);
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
