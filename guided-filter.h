#pragma once
#include "image.h"
#include "time.h"
#include "map.h"

void guidedFilter(ref<ImageF> q, ref<ImageF> I);

void guidedFilter(const ImageF& q, ref<ImageF> I, const ImageF& p);

extern map<string, Time> times;
