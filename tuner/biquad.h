#pragma once
#include "math.h"

// Biquad filter
struct Biquad {
    float a1,a2,b0,b1,b2;
    float x1=0, x2=0, y1=0, y2=0;
    float operator ()(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1, x1=x, y2=y1, y1=y;
        return y;
    }
    void reset() { x1=0, x2=0, y1=0, y2=0; }
};

// High pass filter
//  H(s) = s^2 / (s^2 + s/Q + 1)
struct HighPass : Biquad {
    HighPass(real f/*, real bw*/) {
        real w0 = 2*PI*f;
        //real alpha = sin(w0)*sinh(ln(2)/2*bw*w0/sin(w0));
        real Q=1./sqrt(2.); real alpha = sin(w0)/(2*Q);
        real a0 = 1+alpha;
        a1 = -2*cos(w0)/a0, a2 = (1-alpha)/a0;
        b0 = ((1+cos(w0))/2)/a0, b1 = -(1+cos(w0))/a0, b2 = ((1+cos(w0))/2)/a0;
    }
};

// Notch filter
// H(s) = (s^2 + 1) / (s^2 + s/Q + 1)
struct Notch : Biquad {
    Notch(real f, real Q/*bw*/) {
        real w0 = 2*PI*f;
        //real alpha = sin(w0) * sinh(ln(2)/2*bw*w0/sin(w0));
        real alpha = sin(w0)/(2*Q);
        real a0 = 1+alpha;
        a1 = -2*cos(w0)/a0, a2 = (1-alpha)/a0;
        b0 = 1/a0, b1 = -2*cos(w0)/a0, b2 = 1/a0;
    }
};
