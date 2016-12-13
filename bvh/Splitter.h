#ifndef SPLITTER_HPP_
#define SPLITTER_HPP_

#include "math/Box.h"

namespace Tungsten {

namespace Bvh {

struct SplitInfo
{
    Box3fp lBox, rBox;
    Box3fp lCentroidBox, rCentroidBox;
    int dim;
    uint32 idx;
    float cost;
};

namespace Splitter {
    static constexpr float IntersectionCost = 1.0f;
    static constexpr float TraversalCost = 1.0f;
};

}

}

#endif /* SPLITTER_HPP_ */
