#pragma once
#include "matrix.h"
#include "volume.h"

void project(const ImageF& target, const VolumeF& source, mat4 projection);
void projectTrilinear(const ImageF& target, const VolumeF& source, mat4 projection);
void update(const VolumeF& target, const ImageF& source, mat4 projection);
