#pragma once
#include "image.h"
#include "time.h"
#include "map.h"

void guidedFilter(const Image& target, const Image8& Y, const Image8& U, const Image8& V);

extern map<string, Time> times;
