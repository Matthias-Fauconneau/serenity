#include "string.h"
#include "file.h"
#include "music.h"
#include <sys/mman.h>
#include <unistd.h>
#include "math.h"
#include "lac.h"

void Sampler::open(const string& path) {
    // parse sfz and mmap samples
    Sample group;
    int start=::time();
    string folder = section(path,'/',0,-2)+"/"_;
    TextStream s = mapFile(path);
    Sample* sample=0;
    for(;;) {
        while(*s==' '||*s=='\n'||*s=='\r') s++;
        if(*s == 0) break;
        if(s.match("<group>"_)) { group=Sample(); sample = &group; }
        else if(s.match("<region>"_)) { samples<<group; sample = &samples.last();  }
        else if(s.match("//"_)) {
            while(*s && *s!='\n' && *s!='\r') s++;
            while(*s=='\n' || *s=='\r') s++;
        }
        else {
            string key = section(s,'='); s+=key.size; s++;/*=*/
            string value = s.word();
            if(key=="sample"_) {
                if(::time()-start > 1000) { start=::time(); log("Loading..."_,samples.size); }
                string path = folder+replace(value,'\\','/');
                auto file = mapFile(path);
                sample->data = file.data+4; sample->size=*(int*)file.data;
                mlock(sample->data, min(48000*6,file.size)); //TODO: compute real _compressed_ size for first second
            }
            else if(key=="trigger"_) sample->trigger = value=="release"_;
            else if(key=="lovel"_) sample->lovel=toInteger(value);
            else if(key=="hivel"_) sample->hivel=toInteger(value);
            else if(key=="lokey"_) sample->lokey=toInteger(value);
            else if(key=="hikey"_) sample->hikey=toInteger(value);
            else if(key=="pitch_keycenter"_) sample->pitch_keycenter=toInteger(value);
            else if(key=="ampeg_release"_) sample->releaseTime=(48000*toInteger(value))/1024*1024;
            else if(key=="amp_veltrack"_) sample->amp_veltrack=toInteger(value);
            else if(key=="rt_decay"_) sample->rt_decay=toInteger(value);
            else if(key=="volume"_) sample->volume=exp10(toInteger(value)/20.0);
            else error("unknown opcode"_,key);
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
    Note* released=0;
    int trigger=0;
    if(vel==0) {
        for(Note& note : active) {
            if(note.key==key) {
                note.end = note.position + note.sample->releaseTime;
                trigger=1; released=&note; vel = note.vel; //for release sample
            }
        }
    }
    for(const Sample& s : samples) {
        if(trigger == s.trigger && key >= s.lokey && key <= s.hikey && vel >= s.lovel && vel <= s.hivel) {
            int shift = 1+key-s.pitch_keycenter;
            assert(shift>=0 && shift<3,"TODO: pitch shift > 1"_);
            float level = float(vel*vel)/(127*127);
            level = 1-(s.amp_veltrack/100.0*(1-level));
            level *= s.volume;
            if(released) {
                float noteLength = float(released->end)/48000;
                float attenuation = exp10(-s.rt_decay * noteLength / 20);
                level *= attenuation;
            }
            active << Note{ &s, Codec(array<byte>(s.data,s.size)), 0, s.size/6 + s.releaseTime, key, vel, shift, level };
            break; //assume only one match
        }
    }
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
        int release = n.end-n.position;
        int sustain = release - n.sample->releaseTime;
        int length = n.sample->size/6 - n.position;
        if( sustain >= frame && length >= frame ) {
            sustain = min(sustain,length);
            for(const float* end=d+frame*2;d<end;d+=2) {
                d[0] += level*n.decode(0);
                d[1] += level*n.decode(1);
            }
            n.position += frame;
        } else if( release >= frame && length >= frame ) {
            float reciprocal = level/n.sample->releaseTime;
            const float* end=d+frame*2;
            for(int i=release;d<end;d+=2,i--) {
                d[0] += (reciprocal * i) * n.decode(0);
                d[1] += (reciprocal * i) * n.decode(1);
            }
            n.position += frame;
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

void Sampler::sync() {
    if(!record) return;
    lseek(record,4,SEEK_SET); write(record,raw<int32>(36+time)); lseek(record,0,SEEK_END);
}
