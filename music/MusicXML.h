#pragma once
#include "array.h"
#include "notation.h"

struct MusicXML {
    array<Sign> signs;
    uint divisions; // Time unit per quarter note
    MusicXML(string document);
};
