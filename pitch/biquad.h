#pragma once
#include "math.h"

// Biquad filter
struct Biquad {
    float a1,a2,b0,b1,b2;
    float x1=0, x2=0, y1=0, y2=0;
	inline float operator ()(float x) {
        float y = b0*x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        x2=x1, x1=x, y2=y1, y1=y;
        return y;
    }
    void reset() { x1=0, x2=0, y1=0, y2=0; }
};

// Band pass filter
// H(s) = (s/Q) / (s^2 + s/Q + 1)
struct BandPass : Biquad {
    BandPass(double f, double Q/*bw*/) {
        double w0 = 2*PI*f;
        //double alpha = sin(w0) * sinh(ln(2)/2*bw*w0/sin(w0));
        double alpha = sin(w0)/(2*Q);
        double a0 = 1+alpha;
		a1 = -2*cos(w0)/a0, a2 = (1-alpha)/a0;
		b0 = alpha/a0, b1 = 0, b2 = -alpha/a0;
	}
};
