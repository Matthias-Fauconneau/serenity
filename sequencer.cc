 
class Sequencer : public Poll {
    const int latency = 1024;
    snd_seq_t* seq;
    array<uint8> pressed{128};
    array<uint8> sustained{128};
    bool sustain=false;
    Sampler* sampler=0;
    int maxVelocity=96;
    string record;
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
    virtual ~Sequencer() { sync(); }
    pollfd poll() { pollfd p; snd_seq_poll_descriptors(seq,&p,1,POLLIN); return p; }
    void setSampler(Sampler* sampler) { this->sampler=sampler; }
    void setRecord(bool record) { this->record=record; }
    bool event(pollfd) {
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
                //expected.removeAll(note); followScore();
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
        return true;
    }
    void recordMID(const string& path) { record=copy(path); }
    void sync() {
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

        int fd = open(strz(record).data,O_CREAT|O_WRONLY|O_TRUNC,0666);
        struct { char name[4]={'M','T','h','d'}; int32 size=swap32(6); int16 format=swap16(0);
                 int16 trackCount=swap16(1); int16 timeDivision=swap16(500); } __attribute__ ((packed)) MThd;
        write(fd,MThd);
        struct { char name[4]={'M','T','r','k'}; int32 size=0; } __attribute__ ((packed)) MTrk; MTrk.size=swap32(track.size);
        write(fd,MTrk);
        write(fd,track.data,track.size);
        close(fd);
        record.clear();
    }
};
