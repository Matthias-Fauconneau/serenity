#include <math.h>
#include <sys/resource.h>
#include <alsa/asoundlib.h>
#include <linux/input.h>
#undef assert
#include "common.h"

extern "C" {
struct SpeexResamplerState;
SpeexResamplerState *speex_resampler_init(uint nb_channels, uint in_rate, uint out_rate, int quality, int *err);
int speex_resampler_process_interleaved_float(SpeexResamplerState *st,const float *in,uint *in_len,float *out,uint *out_len);
}

struct AudioSource {
    virtual float* read(int pcmTime, int period)=0;
};
class AudioOutput {
    snd_pcm_t* pcm=0;
    snd_pcm_uframes_t period;
    uint pcmTime=0;
    AudioSource* source=0;
public:
    AudioOutput() { /// setup PCM output
        snd_pcm_open(&pcm,"default",SND_PCM_STREAM_PLAYBACK,SND_PCM_NONBLOCK|SND_PCM_NO_SOFTVOL);
        snd_pcm_hw_params_t *hw; snd_pcm_hw_params_alloca(&hw); snd_pcm_hw_params_any(pcm,hw);
        snd_pcm_hw_params_set_access(pcm,hw, SND_PCM_ACCESS_MMAP_INTERLEAVED);
        snd_pcm_hw_params_set_format(pcm,hw, SND_PCM_FORMAT_S16);
        snd_pcm_hw_params_set_rate(pcm,hw, 48000, 0);
        snd_pcm_hw_params_set_channels(pcm,hw, 2);
        snd_pcm_hw_params_set_period_size_first(pcm, hw, &period, 0);
        snd_pcm_uframes_t bufferSize=period*3; snd_pcm_hw_params_set_buffer_size_near(pcm, hw, &bufferSize);
        snd_pcm_hw_params(pcm, hw);
        snd_pcm_sw_params_t *sw; snd_pcm_sw_params_alloca(&sw);
        snd_pcm_sw_params_current(pcm,sw);
        snd_pcm_sw_params_set_avail_min(pcm,sw, period);
        snd_pcm_sw_params_set_period_event(pcm,sw, 1);
        snd_pcm_sw_params(pcm,sw);
    }
    void setSource(AudioSource* source) { this->source=source; }
    pollfd poll() { pollfd p; snd_pcm_poll_descriptors(pcm,&p,1); return p; }
    void event(pollfd p) {
        assert(source,"No AudioSource for AudioOutput");
        unsigned short revents;
        snd_pcm_poll_descriptors_revents(pcm, &p, 1, &revents);
        assert(!(revents & POLLERR),"too slow");
        if(!(revents & POLLOUT)) return;
        snd_pcm_uframes_t frames = snd_pcm_avail(pcm);
        assert(frames >= period,"snd_pcm_avail");
        const snd_pcm_channel_area_t* areas; snd_pcm_uframes_t offset;
        frames=period;
        snd_pcm_mmap_begin(pcm, &areas, &offset, &frames);
        assert(frames == period,"snd_pcm_mmap_begin");
        int16* dst = (int16*)areas[0].addr+offset*2;
        float* buffer = source->read(pcmTime,period);
        for(uint i=0;i<period*2;i++) {
            int s = int(buffer[i])>>10; //TODO: auto normalization
            assert(s >= -32768 && s < 32768,"clip");
            dst[i] = s;
        }
        frames = snd_pcm_mmap_commit(pcm, offset, frames);
        assert(frames == period,"snd_pcm_mmap_commit");
        pcmTime += period;
        if( snd_pcm_state(pcm) == SND_PCM_STATE_PREPARED ) snd_pcm_start(pcm);
    }
};

struct Signal {
    virtual void update(int time)=0;
};
class Sampler : public AudioSource {
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
    bool record = false;
    int16* pcm = 0; int t = 0;
public:
    Sampler(const char* path) {
        // parse sfz and mmap samples
        Sample group;
        int start=time();
        string folder = section(path,'/',0,-2); folder.size++;
        const char* s = (const char*)mapFile(path);
        Sample* sample=0;
        for(;;) {
            while(*s==' '||*s=='\n'||*s=='\r') s++;
            if(*s == 0) break;
            //TODO: comments
            if(match(s,"<group>")) { group = Sample(); sample = &group; }
            else if(match(s,"<region>")) { samples<<group; sample = &samples.last();  }
            else if(match(s,"//")) {
                while(*s && *s!='\n' && *s!='\r') s++;
                while(*s=='\n' || *s=='\r') s++;
            }
            else {
                string key = section(s,'='); s+=key.size; s++;/*=*/
                const char* v=s; while(*s!=' '&&*s!='\n'&&*s!='\r') s++; string value(v,s);
                if(key=="sample") {
                    if(time()-start > 1000) { start=time(); log("Loading...",samples.size); }
                    string path = folder+value.replace('\\','/')+string("\0",1);
                    sample->data = (uint8*)mapFile(path.data, &sample->size);
                    const int wavHeader=44; sample->data += wavHeader, sample->size -= wavHeader;
                    mlock(sample->data, min(256*1024,sample->size)); //TODO: monolithic file for sequential load
                }
                else if(key=="trigger") sample->trigger = value=="release";
                else if(key=="lovel") sample->lovel=toInteger(value);
                else if(key=="hivel") sample->hivel=toInteger(value);
                else if(key=="lokey") sample->lokey=toInteger(value);
                else if(key=="hikey") sample->hikey=toInteger(value);
                else if(key=="pitch_keycenter") sample->pitch_keycenter=toInteger(value);
                else if(key=="ampeg_release") sample->releaseTime=(48000*toInteger(value))/1024*1024;
                else if(key=="amp_veltrack") sample->amp_veltrack=toInteger(value);
                else if(key=="rt_decay") sample->rt_decay=toInteger(value);
                else if(key=="volume") sample->volume=exp10(toInteger(value)/20.0);
                else assert(0,"unknown opcode",key);
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
    void connect(Signal* signal) { this->signal=signal; }
    void setRecord(bool record) { this->record=record; }
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
    float* read(int pcmTime, int period) {
        assert(period==layers[1].size,"period != 1024");
        if(signal) signal->update(pcmTime);
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
            for(int i=0;i<period*2;i++,t++) {
                int s = int(layers[1].buffer[i])>>10; //TODO: auto normalization
                assert(s >= -32768 && s < 32768,"clip");
                pcm[t] = s;
            }
        }
        return layers[1].buffer;
     }
     void writeWAV(const char* path) {
         struct { char RIFF[4]={'R','I','F','F'}; int32 size; char WAVE[4]={'W','A','V','E'}; char fmt[4]={'f','m','t',' '};
                  int32 headerSize=16; int16 compression=1; int16 channels=2; int32 rate=48000; int32 bps=48000*4;
                  int16 stride=4; int16 bitdepth=16; char data[4]={'d','a','t','a'}; } __attribute__ ((packed)) header;
         int fd = open(path,O_CREAT|O_WRONLY|O_TRUNC,0666);
         header.size=36+t;
         write(fd,header);
         write(fd,pcm,t*sizeof(int16));
     }
};

class MidiFile : public Signal {
    enum { NoteOff=8, NoteOn, Aftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend, Meta };
    enum { SequenceNumber, Text, Copyright, TrackName, InstrumentName, Lyrics, Marker, Cue, ChannelPrefix=0x20,
           EndOfTrack=0x2F, Tempo=0x51, Offset=0x54, TimeSignature=0x58, KeySignature, SequencerSpecific=0x7F };
    struct Track { const uint8* begin=0; const uint8* data=0; const uint8* end=0; int time=0; int type=0; };

    array<Track> tracks;
    int trackCount=0;
    int midiClock=0;
    Sampler* sampler=0;
public:
    MidiFile(const char* path) { /// parse MIDI header
        int size;
        const uint8* s = (const uint8*)mapFile(path,&size), *fileEnd = s+size;
        int16 nofChunks = s[10]<<8|s[11];
        midiClock = 48*60000/120/(s[12]<<8|s[13]); //48Khz clock
        s+=14;
        for(int i=0; s<fileEnd && i<nofChunks;i++) {
            int tag = *(int*)s; int length = s[4]<<24|s[5]<<16|s[6]<<8|s[7]; s+=8;
            if( tag == *(int*)"MTrk") {
                Track track = { s, s, s+length, 0, 0 };
                while(*track.begin++&0x80) {} //ignore first time to revert decode order
                track.data = track.begin;
                tracks << track;
            }
            s += length;
        }
    }
    void setSampler(Sampler* sampler) { this->sampler=sampler; }
    void read(Track& track, int time, bool play) {
        if(track.data>=track.end) return;
        while(track.time < time) {
            const uint8*& s = track.data;
            int type=track.type, vel=0, key=*s++;
            if(key & 0x80) { type=key>>4; key=*s++; }
            if( type == NoteOn) vel=*s++;
            else if( type == NoteOff || type == Aftertouch || type == Controller || type == PitchBend ) s++;
            else if( type == ProgramChange || type == ChannelAftertouch ) {}
            else if( type == Meta ) {
                uint8 c=*s++; int len=c&0x7f; if(c&0x80){ c=*s++; len=(len<<7)|(c&0x7f); }
                s+=len;
            }
            track.type = type;

            if(play) {
                if(type==NoteOn) sampler->event(key,vel);
                else if(type==NoteOff) sampler->event(key,0);
            }

            if(s>=track.end) return;
            uint8 c=*s++; int t=c&0x7f;
            if(c&0x80){c=*s++;t=(t<<7)|(c&0x7f);if(c&0x80){c=*s++;t=(t<<7)|(c&0x7f);if(c&0x80){c=*s++;t=(t<<7)|c;}}}
            track.time += t*midiClock;
        }
    }
    void seek(int time) {
        for(int i=0;i<tracks.size;i++) { Track& track=tracks[i];
            if(time < track.time) { track.time=0; track.data=track.begin; }
            read(track,time,false);
            track.time -= time;
        }
    }
    void update(int time) {
        assert(sampler,"No Sampler for MidiFile");
        for(int i=0;i<tracks.size;i++) read(tracks[i],time,true);
    }
};

class Sequencer {
    const int latency = 1024;
    snd_seq_t* seq;
    array<uint8> pressed{128};
    array<uint8> sustained{128};
    bool sustain=false;
    Sampler* sampler=0;
    int maxVelocity=96;
    bool record=false;
    struct Event { int16 time; uint8 key; uint8 vel; };
    array<Event> events;
    int lastTick=0;
public:
    Sequencer() {
        snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
        snd_seq_set_client_name(seq,"Piano");
        snd_seq_create_simple_port(seq,"Input",SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,SND_SEQ_PORT_TYPE_APPLICATION);
        assert(snd_seq_connect_from(seq,0,20,0)==0,"MIDI controller not connected");
    }
    pollfd poll() { pollfd p; snd_seq_poll_descriptors(seq,&p,1,POLLIN); return p; }
    void setSampler(Sampler* sampler) { this->sampler=sampler; }
    void setRecord(bool record) { this->record=record; }
    void event() {
        assert(sampler,"No Sampler for Sequencer");
        snd_seq_event_t* ev; snd_seq_event_input(seq, &ev);
        if(ev->type == SND_SEQ_EVENT_NOTEON) {
            int key = ev->data.note.note;
            int vel = ev->data.note.velocity;
            if( vel == 0 ) {
                pressed.removeOne(key);
                if(sustain) sustained << key;
                else {
                    sampler->event(key,0);
                    if(record) {
                        int tick = time();
                        events << Event{(int16)(tick-lastTick), (uint8)key, (uint8)0};
                        lastTick = tick;
                    }
                }
            } else {
                sustained.removeOne(key);
                pressed << key;
                if(vel>maxVelocity) maxVelocity=vel;
                sampler->event(key, vel*127/maxVelocity);
                if(record) {
                    int tick = time();
                    events << Event{(int16)(tick-lastTick), (uint8)key, (uint8)vel};
                    lastTick = tick;
                }
                //expected.removeAll(note); followScore(); //TODO: UI
            }
        } else if( ev->type == SND_SEQ_EVENT_CONTROLLER ) {
            if(ev->data.control.param==64) {
                sustain = (ev->data.control.value != 0);
                if(!sustain) {
                    for(int key : sustained) sampler->event(key,0);
                    sustained.clear();
                }
            }
        }
        snd_seq_free_event(ev);
    }
    void writeMID(const char* path) {
        array<uint8> track;
        for(Event e : events) {
            int v=e.time;
            if(v >= 0x200000) track << uint8(((v>>21)&0x7f)|0x80);
            if(v >= 0x4000) track << uint8(((v>>14)&0x7f)|0x80);
            if(v >= 0x80) track << uint8(((v>>7)&0x7f)|0x80);
                        track << uint8(v&0x7f);
            track << (9<<4) << e.key << e.vel;
        }
        track << 0x00 << 0xFF << 0x2F << 0x00; //EndOfTrack

        int fd = open(path,O_CREAT|O_WRONLY|O_TRUNC,0666);
        struct { char name[4]={'M','T','h','d'}; int32 size=swap32(6); int16 format=swap16(0);
                 int16 trackCount=swap16(1); int16 timeDivision=swap16(500); } __attribute__ ((packed)) MThd;
        write(fd,MThd);
        struct { char name[4]={'M','T','r','k'}; int32 size=0; } __attribute__ ((packed)) MTrk; MTrk.size=swap32(track.size);
        write(fd,MTrk);
        write(fd,track.data,track.size);
        close(fd);
    }
};

class Input {
    int fd;
public:
    Input() { fd = open("/dev/input/event2", O_RDONLY); }
    pollfd poll() { return pollfd{fd,POLLIN,0}; }
    bool event() {
        input_event e; read(fd,&e,sizeof(e));
        if(e.type == EV_KEY && e.code==KEY_F4) return true;
        return false;
    }
};

int main(int argc, const char** argv) {
    const char* sfz=0; const char* mid=0; const char* wav=0;
    for(int i=1;i<argc;i++) {
        if(endsWith(argv[i],".sfz")) sfz=argv[i];
        else if(endsWith(argv[i],".mid")) mid=argv[i];
        else if(endsWith(argv[i],".wav")) wav=argv[i];
    }
    if(!sfz) { log("Usage: sfzplay /path/to/instrument.sfz [/path/to/music.mid] [/path/to/output.wav]"); return 0; }

    Sampler* sampler = new Sampler(sfz);
    if(wav && !exists(wav)) sampler->setRecord(true);
    AudioOutput* pcm = new AudioOutput();
    pcm->setSource(sampler);
    Sequencer* seq = new Sequencer();
    seq->setSampler(sampler);
    if(mid && !exists(mid)) seq->setRecord(true);
    Input* input = new Input();
    pollfd pollfd[] = {pcm->poll(), seq->poll(), input->poll()};

    if(mid && exists(mid)) {
        MidiFile* midi = new MidiFile(mid);
        sampler->connect(midi); midi->setSampler(sampler);
        //midi->seek(80*48000);
    }

    setpriority(PRIO_PROCESS,0,-20);
    for(;;) {
        assert(poll(pollfd,3,-1)>0,"poll");
        if(pollfd[0].revents) pcm->event(pollfd[0]); //PCM
        if(pollfd[1].revents) seq->event();
        if(pollfd[2].revents) if(input->event()) break;
    }
    if(mid && !exists(mid)) seq->writeMID(mid);
    if(wav && !exists(wav)) sampler->writeWAV(wav);
    return 0;
}
