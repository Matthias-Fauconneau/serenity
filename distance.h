#pragma once
#include "volume.h"

/// Returns a 32bit distance field volume from a 32bit binary segmented volume
void distance(Volume32& target, const Volume32& source);
