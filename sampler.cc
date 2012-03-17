#include "sampler.h"
#include "stream.h"
#include "string.h"
#include "file.h"
#include "flac.h"
#include "time.h"

#include <math.h>
#include <sys/mman.h>
#include <unistd.h>

#include "array.cc"

void Sampler::open(const string& path) {
    // parse sfz and mmap samples
    Sample group;
    int start=getRealTime();
    Stream s(mapFile(path));
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
                if(getRealTime()-start > 1000) { start=getRealTime(); log("Loading..."_,samples.size()); }
                string path = folder+replace(value,"\\"_,"/"_);
                auto file = mapFile(path);
                sample->data = file.data(); sample->size=file.size();
                madvise((void*)sample->data, file.size(), MADV_SEQUENTIAL);
                mlock((void*)sample->data, min(file.size(),1024*512u));
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
            else if(key=="volume"_) sample->volume=exp10(toInteger(value)/20.0);
            else error("Unknown opcode"_,key);
        }
    }

    for(int i=0;i<3;i++) { Layer& l=layers[i]; //setup layers and pitch shifting
        l.size = round(period*exp2((i-1)/12.0));
        l.buffer = new float[2*l.size];
        if(l.size!=period) new (&l.resampler) Resampler(2, l.size, period);
    }
}

void Sampler::event(int key, int velocity) {
    //log(key,velocity);
    int release=0;
    if(velocity==0) {
        for(Note& note : active) if(note.release && note.key==key) {
            release=note.time-note.remaining; velocity = note.velocity; //schedule release sample
            note.remaining = min(note.remaining,note.release); //schedule active sample fade out
        }
        if(!release) { log("double release"_); return; }
    }
    for(const Sample& s : samples) {
        if(!!release == s.trigger && key >= s.lokey && key <= s.hikey && velocity >= s.lovel && velocity <= s.hivel) {
            Note note(array<byte>(s.data,s.size));
            note.remaining=note.time;
            note.release=s.release;
            note.key=key;
            int shift = key-s.pitch_keycenter; assert(shift>=-1 && shift<=1,"unsupported pitch shift"_,shift);
            note.layer=1+shift;
            note.velocity=velocity;
            note.level = (1-(s.amp_veltrack/100.0*(1-float(velocity*velocity)/(127*127)))) * s.volume;
            if(release) note.level *= exp10(-s.rt_decay * release/48000.0 / 20); //attenuation
            //else log(key,velocity,note.level);
            active << move(note);
            return; //assume only one match
        }
    }
    if(!release) error("Missing sample"_,key,velocity);
}

void Sampler::read(int16 *output, uint period) {
    assert(period==layers[1].size,"period != 1024"_);
    timeChanged.emit(time);
    for(Layer& layer : layers) layer.active=false;
#if DEBUG
    static int cpu=getCPUTime(), real=getRealTime();
    if(getRealTime()-real>1000) { log("active",active.size(),"total",getCPUTime()-cpu,profile); real=getRealTime(); cpu=getCPUTime(); profile.clear(); }
#endif
    for(uint i=0;i<active.size();i++) { Note& n = active[i];
        auto& layer = layers[n.layer];
        int frame = layer.size;
        if(n.remaining < frame) { active.removeAt(i); i--; continue; } //released
        if(!layer.active) { clear(layer.buffer, 2*layer.size); layer.active = true; } //clear if first note in layer (could be avoided)
        if(n.remaining >= n.release) { //sustain
            n.remaining -= frame;
            float* out = layer.buffer;
            while(frame > n.blockSize) {
                frame -= n.blockSize;
                int* in = (int*)(n.buffer+n.position);
                profile(mix, for(int i=0; i<2*n.blockSize; i++) out[i] += n.level * float(in[i]); )
                out += 2*n.blockSize;
                profile(decode, n.readFrame(); )
            }
            int* in = (int*)(n.buffer+n.position);
            profile(mix, for(int i=0; i<2*frame; i++)  out[i] += n.level * float(in[i]); )
            n.position += frame;
            n.blockSize -= frame;
        } else { //release
            float reciprocal = n.level / n.release;
            float* out = layer.buffer;
            while(frame > n.blockSize) {
                frame -= n.blockSize;
                int* in = (int*)(n.buffer+n.position);
                profile(mix, for(int i=0; i<2*n.blockSize; i++,n.remaining--) out[i] += n.remaining*reciprocal * float(in[i]); )
                out += 2*n.blockSize;
                profile(decode, n.readFrame(); )
            }
            int* in = (int*)(n.buffer+n.position);
            profile(mix, for(int i=0; i<2*frame; i++,n.remaining--) out[i] += n.remaining*reciprocal * float(in[i]); )
            n.position += frame;
            n.blockSize -= frame;
        }
    }
    bool anyActive = layers[1].active;
    for(int i=0;i<=2;i+=2) { if(!layers[i].active) continue;
        int in = layers[i].size, out = period;
        profile(resample, layers[i].resampler.filter(layers[i].buffer,&in, layers[1].buffer, &out, anyActive); )
        anyActive=true; //directly mix if unresampled layer or any previous layer is active
    }
    if(anyActive) {
        float* in = layers[1].buffer;
        profile(mix, for(uint i=0;i<period*2;i++) output[i] = clip(-32768,int(in[i])>>8,32767);)
    } else clear(output,period*2); //no active note -> play silence (TODO: pause alsa)
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
