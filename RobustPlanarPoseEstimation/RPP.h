#pragma once
#include "matrix.h"

bool Rpp(const ref<vec2>& model, const ref<vec2>& image, mat3& Rlu, vec3& tlu, int& i, double& objε, double& imgε);
