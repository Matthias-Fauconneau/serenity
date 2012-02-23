#include "string.h"
#include "file.h"
#include "music.h"
#include "math.h"
#include "lac.h"

#include <sys/mman.h>
#include <unistd.h>

void Sampler::open(const string& path) {
    // parse sfz and mmap samples
    Sample group;
    int start=::time();
    TextStream s = mapFile(path);
    auto folder = section(path,'/',0,-2,true);
    Sample* sample=0;
    for(;;) {
        s.whileAny(" \n\r"_);
        if(!s) break;
        if(s.match("<group>"_)) { group=Sample(); sample = &group; }
        else if(s.match("<region>"_)) { samples<<group; sample = &samples.last();  }
        else if(s.match("//"_)) {
            s.untilAny("\n\r"_);
            s.whileAny("\n\r"_);
        }
        else {
            string key = s.until('=');
            string value = s.untilAny(" \n\r"_);
            if(key=="sample"_) {
                if(::time()-start > 1000) { start=::time(); log("Loading..."_,samples.size); }
                short_string path = folder+replace(value,"\\"_,"/"_);
                auto file = mapFile(path);
                sample->data = file.data+4; sample->size=*(int*)file.data;
                madvise((void*)file.data,file.size,MADV_SEQUENTIAL);
                madvise((void*)file.data,min(1024*1024,file.size),MADV_WILLNEED);
                mlock(sample->data, min(512*1024,file.size));
            }
            else if(key=="trigger"_) sample->trigger = value=="release"_;
            else if(key=="lovel"_) sample->lovel=toInteger(value);
            else if(key=="hivel"_) sample->hivel=toInteger(value);
            else if(key=="lokey"_) sample->lokey=toInteger(value);
            else if(key=="hikey"_) sample->hikey=toInteger(value);
            else if(key=="pitch_keycenter"_) sample->pitch_keycenter=toInteger(value);
            else if(key=="ampeg_release"_) sample->releaseTime=48000*toInteger(value);
            else if(key=="amp_veltrack"_) sample->amp_veltrack=toInteger(value);
            else if(key=="rt_decay"_) sample->rt_decay=toInteger(value);
            else if(key=="volume"_) sample->volume=exp10(toInteger(value)/20.0);
            else error("Unknown opcode"_,key);
        }
    }

    // setup pitch shifting
    buffer = new float[period*2];
    for(int i=0;i<3;i++) { Layer& l=layers[i];
        l.size = round(period*exp2((i-1)/12.0));
        l.buffer = new float[l.size*2];
        if(l.size!=period) new (&l.resampler) Resampler(2, l.size, period);
    }
}

void Sampler::event(int key, int vel) {
    int noteLength=0;
    int trigger=0;
    if(vel==0) {
        for(Note& note : active) {
            if(note.key==key) {
                trigger=1; noteLength=note.sample->size/6-note.remaining; vel = note.vel; //schedule release sample
                note.remaining = min(note.remaining, note.sample->releaseTime); //fade out active sample
                break;
            }
        }
        if(!trigger) return; //double release
    }
    for(const Sample& s : samples) {
        if(trigger == s.trigger && key >= s.lokey && key <= s.hikey && vel >= s.lovel && vel <= s.hivel) {
            int shift = 1+key-s.pitch_keycenter;
            assert(shift>=0 && shift<3,"TODO: pitch shift > 1"_);
            float level = float(vel*vel)/(127*127);
            level = 1-(s.amp_veltrack/100.0*(1-level));
            level *= s.volume;
            if(noteLength) {
                float attenuation = exp10(-s.rt_decay * noteLength/48000.0 / 20);
                level *= attenuation;
            }
            if(active.size>=active.capacity) { //Max polyphony
                int best=0, m=active[0].remaining;
                for(int i=1;i<active.size;i++) if(active[i].remaining<m) m=active[i].remaining, best=i;
                active.removeAt(best);
            }
            active << Note{ &s, Codec(array<byte>(s.data,s.size)), s.size/6, key, vel, shift, level };
            return; //assume only one match
        }
    }
    error("Missing sample"_,key,vel);
}

void Sampler::setup(const AudioFormat&) {}

void Sampler::read(int16 *output, int period) {
    assert(period==layers[1].size,"period != 1024"_);
    timeChanged.emit(time);
    for(Layer& layer : layers)  { clear(layer.buffer,layer.size*2); layer.active=false; }
    for(int i=0;i<active.size;i++) { Note& n = active[i];
        layers[n.shift].active = true;
        const int frame = layers[n.shift].size;
        float* d = layers[n.shift].buffer;
        float level = n.level;
        int sustain = n.remaining - n.sample->releaseTime;
        if( sustain >= frame && n.remaining >= frame ) {
            sustain = min(sustain,n.remaining);
            for(const float* end=d+frame*2;d<end;d+=2) {
                d[0] += level*n.decode(0);
                d[1] += level*n.decode(1);
            }
            n.remaining -= frame;
            madvise(n.decode.pos,2*frame*6,MADV_WILLNEED);
        } else if(n.remaining >= frame) {
            float reciprocal = level/n.sample->releaseTime;
            const float* end=d+frame*2;
            for(int i=n.remaining;d<end;d+=2,i--) {
                d[0] += (reciprocal * i) * n.decode(0);
                d[1] += (reciprocal * i) * n.decode(1);
            }
            n.remaining -= frame;
            madvise(n.decode.pos,2*frame*6,MADV_WILLNEED);
        } else { active.removeAt(i); i--; }
    }
    for(Layer& layer : layers) { if(!layer.resampler || !layer.active) continue;
        int in = layer.size, out = period;
        layer.resampler.filter(layer.buffer,&in,buffer,&out);
        for(int i=0;i<period*2;i++) layers[1].buffer[i] += buffer[i];
    }
    for(int i=0;i<period*2;i++) output[i] = clip(-32768,int(layers[1].buffer[i])>>10,32767);
    if(record) write(record,output,period*2*sizeof(int16));
    time+=period;
}

void Sampler::recordWAV(const string& path) {
    record = createFile(path);
    struct { char RIFF[4]={'R','I','F','F'}; int32 size; char WAVE[4]={'W','A','V','E'}; char fmt[4]={'f','m','t',' '};
        int32 headerSize=16; int16 compression=1; int16 channels=2; int32 rate=48000; int32 bps=48000*4;
        int16 stride=4; int16 bitdepth=16; char data[4]={'d','a','t','a'}; /*size?*/ } __attribute__ ((packed)) header;
    write(record,raw(header));
}
Sampler::~Sampler() {
    if(!record) return;
    lseek(record,4,SEEK_SET); write(record,raw<int32>(36+time)); lseek(record,0,SEEK_END);
}
