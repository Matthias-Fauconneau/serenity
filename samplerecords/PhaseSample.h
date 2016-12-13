#ifndef PHASESAMPLE_HPP_
#define PHASESAMPLE_HPP_

#include "math/Vec.h"

namespace Tungsten {

struct PhaseSample
{
    Vec3f w;
    Vec3f weight;
    float pdf;
};

}

#endif /* PHASESAMPLE_HPP_ */
