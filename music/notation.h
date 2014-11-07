#pragma once
/// notation.h Music notation definitions
typedef unsigned int uint;
typedef unsigned long long uint64;
typedef __SIZE_TYPE__ 	size_t;

enum ClefSign { Bass, Treble };
enum Accidental { None, Flat /*♭*/, Sharp /*♯*/, Natural /*♮*/ };
enum Duration { Whole, Half, Quarter, Eighth, Sixteenth };
//enum RestDuration { Semibreve, Minim, Crotchet, Quaver, Semiquaver };
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
    enum Tie { NoTie, TieStart, TieContinue, TieStop } tie;
    bool dot:1;
    bool slur:1; // toggle
    bool grace:1;
    bool slash:1;
    bool staccato:1;
    bool tenuto:1;
    bool accent:1;
    bool stem:1; // 0: down, 1: up
	uint key; // MIDI key
	size_t glyphIndex;
};
struct Rest {
    Duration duration;
};
struct Measure { uint measure, page, pageLine, lineMeasure; };
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
	uint64 time; // Absolute time offset
    uint duration;
	uint staff; // Staff index
	enum {
		Invalid,
		Note, Rest, Clef, // Staff
		Metronome, // Top
		Dynamic, Wedge, // Middle
		Pedal, // Bottom
		Measure, KeySignature, TimeSignature // Across
	} type;
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
inline bool operator <=(const Sign& a, const Sign& b) {
    if(a.time==b.time) {
        if(a.type==Sign::Note && b.type==Sign::Note) return a.note.step <= b.note.step;
    }
    return a.time <= b.time;
}
