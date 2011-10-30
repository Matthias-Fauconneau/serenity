
class MidiFile {
    enum { NoteOff=8, NoteOn, Aftertouch, Controller, ProgramChange, ChannelAftertouch, PitchBend, Meta };
    enum { SequenceNumber, Text, Copyright, TrackName, InstrumentName, Lyrics, Marker, Cue, ChannelPrefix=0x20,
           EndOfTrack=0x2F, Tempo=0x51, Offset=0x54, TimeSignature=0x58, KeySignature, SequencerSpecific=0x7F };
    enum State { Seek=0, Play=1, Sort=2 };
    struct Track { const uint8* begin=0; const uint8* data=0; const uint8* end=0; uint time=0; int type=0; };
    array<Track> tracks;
    int trackCount=0;
    int midiClock=0;
    Sampler* sampler=0;
    map<int, map<int, int> > sort; //[chronologic][bass to treble order] = index
public:
    MidiFile(const string& path) { /// parse MIDI header
        array<uint8> file = mapFile(path);
        const uint8* s = file.data;
        int16 nofChunks = s[10]<<8|s[11];
        midiClock = 48*60000/120/(s[12]<<8|s[13]); //48Khz clock
        s+=14;
        for(int i=0; s<file.data+file.size && i<nofChunks;i++) {
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
    virtual ~MidiFile() {}
    void setSampler(Sampler* sampler) { this->sampler=sampler; connect(sampler->update,update); }
    void read(Track& track, uint time, State state) {
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

            if(state==Play) {
                if(type==NoteOn) sampler->event(key,vel);
                else if(type==NoteOff) sampler->event(key,0);
            } else if(state==Sort) {
                sort[e.tick][e.note] =
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
            read(track,time,Seek);
            track.time -= time;
        }
    }
    void update(int time) {
        assert(sampler,"No Sampler for MidiFile");
        for(int i=0;i<tracks.size;i++) read(tracks[i],time,Play);
    }
};
