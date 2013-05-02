#pragma once
#include "volume.h"

uint maximum(const Volume16& source);

template<int size> void smooth(Volume16& target, const Volume16& source);
