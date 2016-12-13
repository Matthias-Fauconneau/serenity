#pragma once
#include "math/Box.h"

struct SplitInfo
{
    Box3fp lBox, rBox;
    Box3fp lCentroidBox, rCentroidBox;
    int dim;
    uint32 idx;
    float cost;
};

static constexpr float IntersectionCost = 1.0f;
static constexpr float TraversalCost = 1.0f;
