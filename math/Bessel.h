#pragma once
#include "Polynomial.h"
#include <cmath>
// Xiaogang Zhang, John Maddock - Boost Software License

static constexpr double P1[] = {
     -1.4258509801366645672e+11,
      6.6781041261492395835e+09,
     -1.1548696764841276794e+08,
      9.8062904098958257677e+05,
     -4.4615792982775076130e+03,
      1.0650724020080236441e+01,
     -1.0767857011487300348e-02
};
static constexpr double Q1[] = {
     4.1868604460820175290e+12,
     4.2091902282580133541e+10,
     2.0228375140097033958e+08,
     5.9117614494174794095e+05,
     1.0742272239517380498e+03,
     1.0,
     0.0
};
static constexpr double P2[] = {
     -1.7527881995806511112e+16,
      1.6608531731299018674e+15,
     -3.6658018905416665164e+13,
      3.5580665670910619166e+11,
     -1.8113931269860667829e+09,
      5.0793266148011179143e+06,
     -7.5023342220781607561e+03,
      4.6179191852758252278e+00
};
static constexpr double Q2[] = {
     1.7253905888447681194e+18,
     1.7128800897135812012e+16,
     8.4899346165481429307e+13,
     2.7622777286244082666e+11,
     6.4872502899596389593e+08,
     1.1267125065029138050e+06,
     1.3886978985861357615e+03,
     1.0
};
static constexpr double PC[] = {
    -4.4357578167941278571e+06,
    -9.9422465050776411957e+06,
    -6.6033732483649391093e+06,
    -1.5235293511811373833e+06,
    -1.0982405543459346727e+05,
    -1.6116166443246101165e+03,
     0.0
};
static constexpr double QC[] = {
    -4.4357578167941278568e+06,
    -9.9341243899345856590e+06,
    -6.5853394797230870728e+06,
    -1.5118095066341608816e+06,
    -1.0726385991103820119e+05,
    -1.4550094401904961825e+03,
     1.0
};
static constexpr double PS[] = {
     3.3220913409857223519e+04,
     8.5145160675335701966e+04,
     6.6178836581270835179e+04,
     1.8494262873223866797e+04,
     1.7063754290207680021e+03,
     3.5265133846636032186e+01,
     0.0
};
static constexpr double QS[] = {
     7.0871281941028743574e+05,
     1.8194580422439972989e+06,
     1.4194606696037208929e+06,
     4.0029443582266975117e+05,
     3.7890229745772202641e+04,
     8.6383677696049909675e+02,
     1.0
};
static constexpr double
    x1  =  3.8317059702075123156e+00,
    x2  =  7.0155866698156187535e+00,
    x11 =  9.810e+02,
    x12 =  -3.2527979248768438556e-04,
    x21 =  1.7960e+03,
    x22 =  -3.8330184381246462950e-05;

static constexpr double SQRT_PI = 1.7724538509055160273;


static inline double J1(double x)
{
    if (x == 0.0)
        return 0.0;

    double result;
    double w = std::abs(x);
    if (w <= 4.0) {
        double r = Polynomial::rational<7>(x*x, Data::P1, Data::Q1);
        double factor = w*(w + Data::x1)*((w - Data::x11/256.0) - Data::x12);
        result = factor*r;
    } else if (w <= 8.0) {
        double r = Polynomial::rational<8>(x*x, Data::P2, Data::Q2);
        double factor = w*(w + Data::x2)*((w - Data::x21/256.0) - Data::x22);
        result = factor*r;
    } else {
        double y = 8.0/w;
        double rc = Polynomial::rational<7>(y*y, Data::PC, Data::QC);
        double rs = Polynomial::rational<7>(y*y, Data::PS, Data::QS);
        double factor = 1.0/(std::sqrt(w)*Data::SQRT_PI);
        double sx = sin(x);
        double cx = cos(x);
        result = factor*(rc*(sx - cx) + y*rs*(sx + cx));
    }

    if (x < 0.0)
        return -result;
    else
        return result;
}
