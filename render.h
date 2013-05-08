#pragma once
#include "volume.h"
#include "matrix.h"

typedef Volume16 VolumeT; // 8bit is faster (half the size and stronger attenuation) but 16bit is nicer (lighter attenuation, less gradient noise)

/// Square roots all values
void squareRoot(Volume8& target, const Volume16& source);

/// Prepares a volume for rendering by square rooting and normalizing values. Also sets empty space to ε to attenuate rays (for fake ambient occlusion)
void render(VolumeT& target, const Volume16& source);

/// Renders a volume using raytracing
Image render(const VolumeT& volume, const Volume8& empty, mat3 view);
