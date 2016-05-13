#pragma once
#include "image.h"
#include "time.h"
#include "map.h"

void guidedFilter(const ImageF& q, const ImageF& I, const ImageF& p, const int R, const float e);
void guidedFilter(ref<ImageF> q, ref<ImageF> I, const int R, const float e);
void guidedFilter(ref<ImageF> q, ref<ImageF> I, ref<ImageF> p, const int R, const float e);
void guidedFilter(const ImageF& q, ref<ImageF> I, const ImageF& p, const int R, const float e);

ImageF guidedFilter(const ImageF& I, const ImageF& p, const int R, const float e);
ImageF guidedFilter(ref<ImageF> I, const ImageF& p, const int R, const float e);

extern map<string, Time> times;
