#pragma once
#include "volume.h"
#include "matrix.h"

/// Square roots all values
void squareRoot(Volume8& target, const Volume16& source);

/// Prepares a volume for rendering by square rooting and normalizing values. Also sets empty space to ε to attenuate rays (for fake ambient occlusion)
void render(Volume8& target, const Volume16& source);

/// Renders a volume using raytracing
void render(Image& target, const Volume8& empty, const Volume8& density, const Volume8& intensity, mat3 view);
