#pragma once
#include "ErrorBsdf.h"
#include "materials/ConstantTexture.h"

ErrorBsdf::ErrorBsdf()
{
    _albedo = std::make_shared<ConstantTexture>(Vec3f(1.0f, 0.0f, 0.0f));
}
