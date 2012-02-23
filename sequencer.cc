#include "music.h"
#include "file.h"

#include <sys/types.h>
#include <alsa/global.h>
struct snd_input_t;
struct snd_output_t;
#include <alsa/conf.h>
//#include <alsa/pcm.h>
#include <alsa/timer.h>
#include <alsa/control.h>
#include <alsa/seq_event.h>
#include <alsa/seq.h>
#include <alsa/seqmid.h>
#include <poll.h>

//#include <time.h>
int time() { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); return ts.tv_sec*1000+ts.tv_nsec/1000000; }

Sequencer::Sequencer() {
    snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0);
    snd_seq_set_client_name(seq,"Piano");
    snd_seq_create_simple_port(seq,"Input",SND_SEQ_PORT_CAP_WRITE|SND_SEQ_PORT_CAP_SUBS_WRITE,SND_SEQ_PORT_TYPE_APPLICATION);
    if(snd_seq_connect_from(seq,0,20,0)) log("MIDI controller not connected");
    registerPoll();
}

pollfd Sequencer::poll() { pollfd p; snd_seq_poll_descriptors(seq,&p,1,POLLIN); return p; }
void Sequencer::event(pollfd) {
    int remaining;
    do {
        snd_seq_event_t* ev;
        remaining = snd_seq_event_input(seq, &ev);
        if(ev->type == SND_SEQ_EVENT_NOTEON) {
            int key = ev->data.note.note;
            int vel = ev->data.note.velocity;
            if( vel == 0 ) {
                pressed.removeOne(key);
                if(sustain) sustained.appendOnce( key );
                else {
                    noteEvent.emit(key,0);
                    if(record) {
                        int tick = time();
                        events << Event((int16)(tick-lastTick), (uint8)key, (uint8)0);
                        lastTick = tick;
                    }
                }
            } else {
                pressed << key;
                if(vel>maxVelocity) maxVelocity=vel;
                noteEvent.emit(key, vel*127/maxVelocity);
                if(record) {
                    int tick = time();
                    events << Event((int16)(tick-lastTick), (uint8)key, (uint8)vel);
                    lastTick = tick;
                }
            }
        } else if( ev->type == SND_SEQ_EVENT_CONTROLLER ) {
            if(ev->data.control.param==64) {
                sustain = (ev->data.control.value != 0);
                if(!sustain) {
                    for(int key : sustained) noteEvent.emit(key,0);
                    sustained.clear();
                }
            }
        }
        snd_seq_free_event(ev);
    } while(remaining>1);
}

void Sequencer::recordMID(const string& path) { record=copy(path); }
Sequencer::~Sequencer() {
    array<byte> track;
    for(Event e : events) {
        int v=e.time;
        if(v >= 0x200000) track << uint8(((v>>21)&0x7f)|0x80);
        if(v >= 0x4000) track << uint8(((v>>14)&0x7f)|0x80);
        if(v >= 0x80) track << uint8(((v>>7)&0x7f)|0x80);
        track << uint8(v&0x7f);
        track << (9<<4) << e.key << e.vel;
    }
    track << 0x00 << 0xFF << 0x2F << 0x00; //EndOfTrack

    int fd = createFile(record);
    struct { char name[4]={'M','T','h','d'}; int32 size=swap32(6); int16 format=swap16(0);
        int16 trackCount=swap16(1); int16 timeDivision=swap16(500); } __attribute__ ((packed)) MThd;
    write(fd,raw(MThd));
    struct { char name[4]={'M','T','r','k'}; int32 size=0; } __attribute__ ((packed)) MTrk; MTrk.size=swap32(track.size);
    write(fd,raw(MTrk));
    write(fd,track);
    close(fd);
    record.clear();
}
