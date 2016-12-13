#ifndef LIGHTSAMPLE_HPP_
#define LIGHTSAMPLE_HPP_

#include "math/Vec.h"

namespace Tungsten {

class Medium;

struct LightSample
{
    Vec3f d;
    float dist;
    float pdf;
    const Medium *medium;
};

}

#endif /* LIGHTSAMPLE_HPP_ */
