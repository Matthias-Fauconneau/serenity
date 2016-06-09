#pragma once
#include "notation.h"

struct ABC {
 array<Sign> signs;
 const uint ticksPerBeat = 12; // Time unit (ticks) per beat (quarter note)

 ABC() {}
 ABC(ref<byte> file);
 explicit operator bool() const { return bool(signs.size); }
};
