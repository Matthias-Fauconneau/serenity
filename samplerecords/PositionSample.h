#pragma once
#include "primitives/IntersectionInfo.h"

struct PositionSample
{
    Vec3f p;
    Vec3f weight;
    float pdf;

    Vec2f uv;
    Vec3f Ng;

    PositionSample() = default;
    PositionSample(const IntersectionInfo &info)
    : p(info.p),
      weight(0.0f),
      pdf(0.0f),
      uv(info.uv),
      Ng(info.Ng)
    {
    }
};
