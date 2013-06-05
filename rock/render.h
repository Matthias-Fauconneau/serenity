#pragma once
#include "volume.h"
#include "matrix.h"

/// Renders a volume using raytracing
void render(Image& target, const Volume8& empty, const Volume8& density, const Volume8& intensity, mat3 view);
