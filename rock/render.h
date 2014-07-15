#pragma once
#include "volume.h"
#include "matrix.h"

/// Renders a volume using raytracing
void render(Image& target, const Volume8& attenuation, const Volume8& intensity, /*const Volume8& empty,*/ mat3 view);
