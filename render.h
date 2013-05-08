#pragma once
#include "volume.h"
#include "matrix.h"

/// Prepares a volume for rendering by square rooting and normalizing values. Also sets empty space to ε to attenuate rays (for fake ambient occlusion)
void render(Volume8& target, const Volume16& source);

/// Renders a volume using raytracing
Image render(const Volume8& volume, mat3 view);
