#pragma once
#include "opencl.h"

float sum(const CLVolume& A);
float SSQ(const CLVolume& A);
float SSE(const CLVolume& A, const CLVolume& B);
float dotProduct(const CLVolume& A, const CLVolume& B);
