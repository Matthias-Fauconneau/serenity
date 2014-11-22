#pragma once
#include "array.h"
#include "notation.h"

struct MusicXML {
    array<Sign> signs;
    uint divisions = 0; // Time unit per quarter note
    MusicXML() {}
    MusicXML(string document, string name="");
    explicit operator bool() const { return signs.size; }
};
