#pragma once
#include "volume.h"
#include "matrix.h"

void tile(Volume& target, const Volume& source);
void clip(Volume& target);
Image render(const Volume& volume, mat3 view);
