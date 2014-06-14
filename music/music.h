#pragma once
typedef unsigned int uint;

enum ClefSign { Bass, Treble };
enum Accidental { None, Flat /*♭*/, Sharp /*♯*/, Natural /*♮*/ };
enum Duration { Whole, Half, Quarter, Eighth, Sixteenth };
enum class Loudness { PPP, PP, P, MP, MF, F, FF, FFF };
enum PedalAction { Ped=-1, Start, Change, PedalStop };
enum WedgeAction { Crescendo, Diminuendo, WedgeStop };

struct Clef {
    ClefSign clefSign;
    int octave;
};
struct Note {
    Clef clef;
    int step; // 0 = C4
    Accidental accidental;
    Duration duration;
    bool dot:1;
    bool slur:1; // toggle
    bool grace:1;
    bool staccato:1;
    bool tenuto:1;
    bool accent:1;
    bool stem:1; // 0: down, 1: up
};
struct Rest {
    Duration duration;
};
struct Measure {
    uint index;
};
struct Pedal {
    PedalAction action;
};
struct Wedge {
    WedgeAction action;
};
struct Dynamic {
    Loudness loudness;
};
struct KeySignature {
    int fifths; // Index on the fifths circle
};
struct TimeSignature {
    uint beats, beatUnit;
};
struct Metronome {
    Duration beatUnit;
    uint perMinute;
};

struct Sign {
    uint time; // Absolute time offset
    uint duration;
    uint staff; // Staff index
    enum { Note, Rest, Measure, Dynamic, Clef, KeySignature, TimeSignature, Metronome, Pedal, Wedge } type;
    union {
        struct Note note;
        struct Rest rest;
        struct Measure measure;
        struct Clef clef;
        struct KeySignature keySignature;
        struct TimeSignature timeSignature;
        struct Metronome metronome;
        struct Dynamic dynamic;
        struct Pedal pedal;
        struct Wedge wedge;
    };
};

inline bool operator <(const Sign& a, const Sign& b) {
    if(a.time==b.time) {
        if(a.type==Sign::Note && b.type==Sign::Note) return a.note.step < b.note.step;
    }
    return a.time < b.time;
}

inline bool operator <=(const Sign& a, const Sign& b) {
    if(a.time==b.time) {
        if(a.type==Sign::Note && b.type==Sign::Note) return a.note.step <= b.note.step;
     }
    return a.time <= b.time;
}
