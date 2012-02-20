#pragma once
#include "media.h"
#include "process.h"
#include "map.h"
#include "vector.h"
#include "stream.h"
#include "lac.h"

int time();

typedef struct _snd_seq snd_seq_t;
struct Sequencer : Poll {
    static const int latency = 1024;
    snd_seq_t* seq;
    array<uint8> pressed{128};
    array<uint8> sustained{128};
    bool sustain=false;
    signal<int,int> noteEvent;
    int maxVelocity=96;
    string record;
    struct Event { int16 time; uint8 key; uint8 vel; Event(int16 time, uint8 key, uint8 vel):time(time),key(key),vel(vel){}};
    array<Event> events;
    int lastTick=0;

    Sequencer();
    pollfd poll();
    void setRecord(bool record);
    void event(pollfd);
    void recordMID(const string& path);
    void sync();
};

struct MidiFile {
    enum { NoteOff=8, NoteOn, Aftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend, Meta };
    enum { SequenceNumber, Text, Copyright, TrackName, InstrumentName, Lyrics, Marker, Cue, ChannelPrefix=0x20,
           EndOfTrack=0x2F, Tempo=0x51, Offset=0x54, TimeSignature=0x58, KeySignature, SequencerSpecific=0x7F };
    enum State { Seek=0, Play=1, Sort=2 };
    struct Track : Stream<> { Track(array<byte> data):Stream(data){} int time=0; int type=0; };
    array<Track> tracks;
    int trackCount=0;
    int midiClock=0;
    map<int, map<int, int> > sort; //[chronologic][bass to treble order] = index
    signal<int, int> noteEvent;

    void open(const string& path);
    void read(Track& track, int time, State state);
    void seek(int time);
    void update(int time);
};

struct Sampler : AudioInput {
    struct Sample {
        const byte* data=0; int size=0; //Sample Definition
        int16 trigger=0; int16 lovel=0; int16 hivel=127; int16 lokey=0; int16 hikey=127; //Input Controls
        int16 pitch_keycenter=60; int32 releaseTime=0; int16 amp_veltrack=100; int16 rt_decay=0; float volume=1; //Performance Parameters
    };
    struct Note { const Sample* sample; Codec decode; int position,end; int key; int vel; int shift; float level; };

    static const int period = 1024; //-> latency

    array<Sample> samples{1024};
    array<Note> active{64};
    struct Layer { int size=0; float* buffer=0; bool active=false; Resampler resampler; } layers[3];
    float* buffer = 0;
    int record;
    int16* pcm = 0; int time = 0;
    signal<int> timeChanged;
    operator bool() { return buffer; }

    void open(const string& path);
    void event(int key, int vel);
    void setup(const AudioFormat& format) override;
    void read(int16* output, int size) override;
    void recordWAV(const string& path);
    void sync();
};

struct Score {
    array<float> staffs;
    array<vec2> positions;
    //array<int> noteIndices;

    void onGlyph(int, vec2, float,const string&, int);
    void onPath(const array<vec2>&);
    void synchronize(array<int> MIDI);

    vec2 lastClef;
    array<vec2> repeats;

    struct Line {
        vec2 a,b;
        //Line(){}
        Line(vec2 a, vec2 b):a(a),b(b){}
        bool operator ==(Line o) const { return a==o.a && b==o.b; }
    };
    array<Line> ties;
    array<Line> tails;
    array<Line> tremolos;
    array<Line> trills;

    struct Note {
        Note() : index(-1), duration(-1) {}
        Note(int index, int duration) : index(index), duration(duration) {}
        int index,duration;
    };
    map<int, map<int, map< int, Note> > > notes; //[staff][x][y]
    map<int, array<vec2> > dots;

    /*struct Debug {
      vec2 pos;
      string text;
      Debug(){}
      Debug(vec2 pos,string text):pos(pos),text(text){}
    };
    array<Debug> debug;*/
};
