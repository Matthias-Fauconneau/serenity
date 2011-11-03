
/// stream
template<typename T> struct Stream {
    const array<T>& data;
    int i=0;
    Stream(const array<T>& data) : data(data) {}
    Stream& operator ++(int) { i++; return *this; }
    Stream& operator +=(int s) { i+=s; return *this; }
    T operator*() { return data[i]; }
    operator array<T>() { return data.slice(i); }
    operator const char*() { return &data.at(i); }
};

struct TextStream : Stream<char> {
    TextStream(const string& data) : Stream(data) {}
    bool match(const string& key);
    long readInteger(int base=10);
    double readFloat(int base=10);
};
bool TextStream::match(const string& key) {
    if(data.slice(i,key.size) == key) { i+=key.size; return true; } else return false;
}
long TextStream::readInteger(int base) { auto b=&data[i]; auto e=b; auto r = ::readInteger(e,base); i+=e-b; return r; }
double TextStream::readFloat(int base) { auto b=&data[i]; auto e=b; auto r = ::readFloat(e,base); i+=e-b; return r; }

struct Sampler : AudioSource {
    struct Sample {
        const uint8* data=0; int size=0; //Sample Definition
        int16 trigger=0; int16 lovel=0; int16 hivel=127; int16 lokey=0; int16 hikey=127; //Input Controls
        int16 pitch_keycenter=60; int32 releaseTime=0; int16 amp_veltrack=100; int16 rt_decay=0; float volume=1; //Performance Parameters
    };
    struct Note { const Sample* sample; const uint8* begin; const uint8* end; int key; int vel; int shift; float level; };

    static const int period = 1024; //-> latency

    array<Sample> samples{1024};
    array<Note> active{64};
    struct Layer { int size=0; float* buffer=0; SpeexResamplerState* resampler=0; } layers[3];
    float* tmp = 0;
    Signal* signal = 0;
    string record;
    int16* pcm = 0; int time = 0;

    Sampler(const string& path) {
        // parse sfz and mmap samples
        Sample group;
        int start=time();
        string folder = section(path,'/',0,-2); folder.size++;
        TextStream s = mapFile(path);
        Sample* sample=0;
        for(;;) {
            while(*s==' '||*s=='\n'||*s=='\r') s++;
            if(*s == 0) break;
            if(s.match(_("<group>"))) { group=Sample(); sample = &group; }
            else if(s.match(_("<region>"))) { samples<<group; sample = &samples.last();  }
            else if(s.match(_("//"))) {
                while(*s && *s!='\n' && *s!='\r') s++;
                while(*s=='\n' || *s=='\r') s++;
            }
            else {
                string key = section(s,'='); s+=key.size; s++;/*=*/
                const char* v=s; while(*s!=' '&&*s!='\n'&&*s!='\r') s++; string value(v,s);
                if(key==_("sample")) {
                    if(time()-start > 1000) { start=time(); log("Loading...",samples.size); }
                    string path = folder+value.replace('\\','/')+string("\0",1);
                    auto file = mapFile(path).slice(44);
                    sample->data = (uint8*)file.data; sample->size=file.size;
                    mlock(sample->data, min(256*1024,sample->size)); //TODO: monolithic file for sequential load
                }
                else if(key==_("trigger")) sample->trigger = value==_("release");
                else if(key==_("lovel")) sample->lovel=toInteger(value);
                else if(key==_("hivel")) sample->hivel=toInteger(value);
                else if(key==_("lokey")) sample->lokey=toInteger(value);
                else if(key==_("hikey")) sample->hikey=toInteger(value);
                else if(key==_("pitch_keycenter")) sample->pitch_keycenter=toInteger(value);
                else if(key==_("ampeg_release")) sample->releaseTime=(48000*toInteger(value))/1024*1024;
                else if(key==_("amp_veltrack")) sample->amp_veltrack=toInteger(value);
                else if(key==_("rt_decay")) sample->rt_decay=toInteger(value);
                else if(key==_("volume")) sample->volume=exp10(toInteger(value)/20.0);
                else fail("unknown opcode",key);
            }
        }

        // setup pitch shifting
        tmp = new float[period*2];
        for(int i=0;i<3;i++) { Layer& l=layers[i];
            l.size = round(period*exp2((i-1)/12.0));
            l.buffer = new float[l.size*2];
            l.resampler = l.size==period ? 0 : speex_resampler_init(2, l.size, period, 5, 0);
        }
    }
    virtual ~Sampler() { sync(); }
    void connect(Signal* signal) { this->signal=signal; }
    void event(int key, int vel) {
        Note* released=0;
        int trigger=0;
        if(vel==0) {
            for(Note& note : active) {
                if(note.key==key) {
                    note.end = note.begin + 6*note.sample->releaseTime;
                    trigger=1; released=&note; vel = note.vel; //for release sample
                }
            }
        }
        for(const Sample& s : samples) {
            if(trigger == s.trigger && key >= s.lokey && key <= s.hikey && vel >= s.lovel && vel <= s.hivel) {
                int shift = 1+key-s.pitch_keycenter;
                assert(shift>=0 && shift<3,"TODO: pitch shift > 1");
                float level = float(vel*vel)/(127*127);
                level = 1-(s.amp_veltrack/100.0*(1-level));
                level *= s.volume;
                if(released) {
                    float noteLength = float(released->end - released->sample->data)/6/48000;
                    float attenuation = exp10(-s.rt_decay * noteLength / 20);
                    level *= attenuation;
                }
                active << Note{ &s, s.data, s.data + s.size + 6*s.releaseTime, key, vel, shift, level };
                break; //assume only one match
            }
        }
    }
    float* read(int period) {
        assert(period==layers[1].size,"period != 1024");
        if(signal) signal->update(time);
        for(Layer layer : layers) clear(layer.buffer,layer.size*2);
        for(int i=0;i<active.size;i++) { Note& n = active[i];
            const int frame = layers[n.shift].size;
            float* d = layers[n.shift].buffer;
            const uint8* &s = n.begin;
#define L float(*(int*)(s-1)>>8) //TODO: fast 24bit to float
#define R float(*(int*)(s+2)>>8)
            float level = n.level;
            int release = (n.end-s)/6;
            int sustain = release - n.sample->releaseTime;
            int length = ((n.sample->data+n.sample->size)-s)/6;
            if( sustain >= frame && length >= frame ) {
                sustain = min(sustain,length);
                for(const float* end=d+frame*2;d<end;d+=2,s+=6) {
                    d[0] += level*L;
                    d[1] += level*R;
                }
            } else if( release >= frame && length >= frame ) {
                float reciprocal = level/n.sample->releaseTime;
                const float* end=d+frame*2;
                for(int i=release;d<end;d+=2,s+=6,i--) {
                    d[0] += (reciprocal * i) * L;
                    d[1] += (reciprocal * i) * R;
                }
            } else { active.removeAt(i); i--; }
        }
        for(Layer layer : layers) { if(!layer.resampler) continue;
            uint in = layer.size, out = period;
            speex_resampler_process_interleaved_float(layer.resampler,layer.buffer,&in,tmp,&out);
            for(int i=0;i<period*2;i++) layers[1].buffer[i] += tmp[i];
        }
        if(record) {
            if(!pcm) pcm = (int16*)mmap(0,4*256*64*1024,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0); //6min
            for(int i=0;i<period*2;i++) {
                int s = int(layers[1].buffer[i])>>10; //TODO: auto normalization
                assert(s >= -32768 && s < 32768,"clip");
                pcm[2*time+i] = s;
            }
        }
        time+=period;
        return layers[1].buffer;
     }
     void recordWAV(const string& path) { record=copy(path); }
     void sync() {
         if(!record) return;
         struct { char RIFF[4]={'R','I','F','F'}; int32 size; char WAVE[4]={'W','A','V','E'}; char fmt[4]={'f','m','t',' '};
                  int32 headerSize=16; int16 compression=1; int16 channels=2; int32 rate=48000; int32 bps=48000*4;
                  int16 stride=4; int16 bitdepth=16; char data[4]={'d','a','t','a'}; } __attribute__ ((packed)) header;
         int fd = createFile(record);
         header.size=36+t;
         write(fd,header);
         write(fd,pcm,t*sizeof(int16));
         record.clear();
     }
};
