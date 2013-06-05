#pragma once
#include "sample.h"
#include "volume.h"

/// Computes histogram of values
Sample histogram(const Volume16& volume, bool cylinder=false);
