#pragma once
#include "volume.h"

/// Shifts all values to the right
void shiftRight(Volume16& target, const Volume16& source, uint shift);

/// Computes one pass of running average
void smooth(Volume16& target, const Volume16& source, uint size, uint shift);
