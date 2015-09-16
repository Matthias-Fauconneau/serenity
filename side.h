#pragma once

void side(int W, const float* Px, const float* Py, const float* Pz,
          const float* Fx, const float* Fy, const float* Fz, float pressure,
          float internodeLength, float tensionStiffness, //float radius,
          int start, int size);
