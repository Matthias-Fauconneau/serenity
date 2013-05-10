#pragma once
#include "volume.h"

typedef array<uint> Histogram;

/// Computes histogram of values (without normalizing)
Histogram histogram(const Volume16& volume, bool cylinder=false);

/// Computes histogram of square roots (without normalizing)
Histogram sqrtHistogram(const Volume16& volume, bool cylinder=false);

Histogram parseHistogram(const ref<byte>& file);

string str(const Histogram& histogram, float scale=1);
