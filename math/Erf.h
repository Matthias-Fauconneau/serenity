#pragma once
#include "Polynomial.h"
// John Maddock - Boost Software License

static const double Y1 = 0.0891314744949340820313;
static const double Y2 = 2.249481201171875;
static const double Y3 = 0.807220458984375;
static const double Y4 = 0.93995571136474609375;
static const double Y5 = 0.98362827301025390625;
static const double Y6 = 0.99714565277099609375;
static const double Y7 = 0.99941349029541015625;

static const double P1[] = {
    -0.000508781949658280665617,
    -0.00836874819741736770379,
     0.0334806625409744615033,
    -0.0126926147662974029034,
    -0.0365637971411762664006,
     0.0219878681111168899165,
     0.00822687874676915743155,
    -0.00538772965071242932965
};
static const double Q1[] = {
    1.0,
    -0.970005043303290640362L,
    -1.56574558234175846809,
     1.56221558398423026363,
     0.662328840472002992063,
    -0.71228902341542847553,
    -0.0527396382340099713954,
     0.0795283687341571680018,
    -0.00233393759374190016776,
     0.000886216390456424707504
};

static const double P2[] = {
    - 0.202433508355938759655,
      0.105264680699391713268,
      8.37050328343119927838,
     17.6447298408374015486,
    -18.8510648058714251895,
    -44.6382324441786960818,
     17.445385985570866523,
     21.1294655448340526258,
    - 3.67192254707729348546
};
static const double Q2[] = {
      1.0,
      6.24264124854247537712,
      3.9713437953343869095,
    -28.6608180499800029974,
    -20.1432634680485188801,
     48.5609213108739935468,
     10.8268667355460159008,
    -22.6436933413139721736,
      1.72114765761200282724
};
static const double P3[] = {
    -0.131102781679951906451,
    -0.163794047193317060787,
     0.117030156341995252019,
     0.387079738972604337464,
     0.337785538912035898924,
     0.142869534408157156766,
     0.0290157910005329060432,
     0.00214558995388805277169,
    -0.679465575181126350155e-6,
     0.285225331782217055858e-7,
    -0.681149956853776992068e-9
};
static const double Q3[] = {
    1.0,
    3.46625407242567245975,
    5.38168345707006855425,
    4.77846592945843778382,
    2.59301921623620271374,
    0.848854343457902036425,
    0.152264338295331783612,
    0.01105924229346489121
};
static const double P4[] = {
    -0.0350353787183177984712,
    -0.00222426529213447927281,
     0.0185573306514231072324,
     0.00950804701325919603619,
     0.00187123492819559223345,
     0.000157544617424960554631,
     0.460469890584317994083e-5,
    -0.230404776911882601748e-9,
     0.266339227425782031962e-11
};
static const double Q4[] = {
    1.0,
    1.3653349817554063097,
    0.762059164553623404043,
    0.220091105764131249824,
    0.0341589143670947727934,
    0.00263861676657015992959,
    0.764675292302794483503e-4
};
static const double P5[] = {
    -0.0167431005076633737133,
    -0.00112951438745580278863,
     0.00105628862152492910091,
     0.000209386317487588078668,
     0.149624783758342370182e-4,
     0.449696789927706453732e-6,
     0.462596163522878599135e-8,
    -0.281128735628831791805e-13,
     0.99055709973310326855e-16
};
static const double Q5[] = {
    1.0,
    0.591429344886417493481,
    0.138151865749083321638,
    0.0160746087093676504695,
    0.000964011807005165528527,
    0.275335474764726041141e-4,
    0.282243172016108031869e-6
};
static const double P6[] = {
    -0.0024978212791898131227,
    -0.779190719229053954292e-5,
     0.254723037413027451751e-4,
     0.162397777342510920873e-5,
     0.396341011304801168516e-7,
     0.411632831190944208473e-9,
     0.145596286718675035587e-11,
    -0.116765012397184275695e-17
};
static const double Q6[] = {
    1.0,
    0.207123112214422517181,
    0.0169410838120975906478,
    0.000690538265622684595676,
    0.145007359818232637924e-4,
    0.144437756628144157666e-6,
    0.509761276599778486139e-9
};
static const double P7[] = {
    -0.000539042911019078575891,
    -0.28398759004727721098e-6,
     0.899465114892291446442e-6,
     0.229345859265920864296e-7,
     0.225561444863500149219e-9,
     0.947846627503022684216e-12,
     0.135880130108924861008e-14,
    -0.348890393399948882918e-21
};
static const double Q7[] = {
    1.0,
    0.0845746234001899436914,
    0.00282092984726264681981,
    0.468292921940894236786e-4,
    0.399968812193862100054e-6,
    0.161809290887904476097e-8,
    0.231558608310259605225e-11
};

static inline double erfInv(double z)
{
    double p, q, s;
    if (z < 0) {
        p = -z;
        q = 1 - p;
        s = -1;
    } else {
        p = z;
        q = 1 - z;
        s = 1;
    }

    double result = 0.0;
    if (p <= 0.5) {
        double g = p*(p + 10.0);
        double r = Polynomial::eval<8>(p, Data::P1)/Polynomial::eval<10>(p, Data::Q1);
        result = g*Data::Y1 + g*r;
    } else if (q >= 0.25) {
        double g = std::sqrt(-2.0*std::log(q));
        double xs = q - 0.25;
        double r = Polynomial::eval<9>(xs, Data::P2)/Polynomial::eval<9>(xs, Data::Q2);
        result = g/(Data::Y2 + r);
    } else {
        double x = std::sqrt(-std::log(q));
        if (x < 3.0) {
            double xs = x - 1.125;
            double R = Polynomial::eval<11>(xs, Data::P3)/Polynomial::eval<8>(xs, Data::Q3);
            result = Data::Y3*x + R*x;
        } else if (x < 6.0) {
            double xs = x - 3;
            double R = Polynomial::eval<9>(xs, Data::P4)/Polynomial::eval<7>(xs, Data::Q4);
            result = Data::Y4*x + R*x;
        } else if (x < 18.0) {
            double xs = x - 6.0;
            double R = Polynomial::eval<9>(xs, Data::P5)/Polynomial::eval<7>(xs, Data::Q5);
            result = Data::Y5*x + R*x;
        } else if (x < 44.0) {
            double xs = x - 18.0;
            double R = Polynomial::eval<8>(xs, Data::P6)/Polynomial::eval<7>(xs, Data::Q6);
            result = Data::Y6*x + R*x;
        } else {
            double xs = x - 44.0;
            double R = Polynomial::eval<8>(xs, Data::P7)/Polynomial::eval<7>(xs, Data::Q7);
            result = Data::Y7*x + R*x;
        }
    }
    return s*result;
}

// The following two functions are based on
// "Handbook of Mathematical Functions" page 299, by Abramowitz and Stegun
template<typename T>
static inline T erfc(T x)
{
    const T p = T(0.32759);
    const T as[] = {T(0.254829592), T(-0.284496736), T(1.421413741), T(-1.453152027), T(1.061405429)};
    T t = T(1.0)/(T(1.0) + p*std::abs(x));
    T ti = std::copysignf(t*std::exp(-x*x), x);
    T result = T(0.0);
    for (int i = 0; i < 5; ++i) {
        result += as[i]*ti;
        ti *= t;
    }
    T constant = T(1.0) - std::copysignf(T(1.0), x);
    return constant + result;
}

template<typename T>
static inline T erfDifference(T x0, T x1)
{
    const T p = T(0.32759);
    const T as[] = {T(0.254829592), T(-0.284496736), T(1.421413741), T(-1.453152027), T(1.061405429)};
    T t0 = T(1.0)/(T(1.0) + p*std::abs(x0));
    T t1 = T(1.0)/(T(1.0) + p*std::abs(x1));
    T ti0 = std::copysignf(t0*std::exp(-x0*x0), x0);
    T ti1 = std::copysignf(t1*std::exp(-x1*x1), x1);
    T result = T(0.0);
    for (int i = 0; i < 5; ++i) {
        result += as[i]*(ti0 - ti1);
        ti0 *= t0;
        ti1 *= t1;
    }
    T constant = std::copysignf(T(1.0), x1) - std::copysignf(T(1.0), x0);
    return constant + result;
}

// Based on "A handy approximation for the error function and its inverse" by Sergei Winitzki
template<typename T>
static inline T erfApprox(T x)
{
    const T a = T(0.147);
    float xSq = x*x;
    return std::copysignf(std::sqrt(T(1.0) - std::exp(-xSq*(T(4.0f*INV_PI) + a*xSq)/(T(1.0) + a*xSq))), x);
}
template<typename T>
static inline T invErfApprox(T x)
{
    const T a = T(0.147);
    const T PiA_2 = T(2.0)/(T(PI)*a);
    T lnX = std::log(T(1.0) - x*x);

    return std::copysignf(std::sqrt(-PiA_2 - lnX*T(0.5) + std::sqrt(sqr(PiA_2 + lnX*T(0.5)) - lnX*(T(1.0)/a))), x);
}
