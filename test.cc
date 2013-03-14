#if 0
#include "parser.h"
struct Calculator {
    Calculator() {
        Parser parser;
        /// Semantic actions used to synthesize attributes
        parser["integer"_]=  (function<float(ref<byte>)>) [](ref<byte> token){ return toInteger(token); };
        parser["add"_] = (function<float(float,float)>) [](float a, float b){ return a+b; };
        parser["sub"_] = (function<float(float,float)>) [](float a, float b){ return a-b; };
        parser["mul"_] = (function<float(float,float)>) [](float a, float b){ return a*b; };
        parser["div"_] = (function<float(float,float)>) [](float a, float b){ return a/b; };
        parser.generate( // S-attributed EBNF grammar for arithmetic expressions
                         "Expr: Term | Expr '+' Term { value: add Expr.value Term.value } | Expr '-' Term { value: sub Expr.value Term.value }""\n"
                         "Term: Factor | Term '*' Factor { value: mul Term.value Factor.value } | Term '/' Factor { value: div Term.value Factor.value }""\n"
                         "Factor: [0-9]+ { value: integer } | '(' Expr ')'""\n"_);
        const ref<byte>& input =  "(10+(21-32)*43)/54"_;
        Node result = parser.parse(input);
        log(input,"=",result.values.at("value"_));
    }
} test;
#endif
#if 1
#include "time.h"
#include "string.h"
typedef double real;
inline real mod(real q, real d) { return __builtin_fmod(q, d); }
const real PI = 3.14159265358979323846;
inline real cos(real t) { return __builtin_cos(t); }
inline real sin(real t) { return __builtin_sin(t); }
inline real tan(real t) { return __builtin_tan(t); }
inline real acos(real t) { return __builtin_acos(t); }
inline real asin(real t) { return __builtin_asin(t); }
inline real atan(real t) { return __builtin_atan(t); }
inline real atan(real y, real x) { return __builtin_atan2(y, x); }
inline real deg(real t) { return t/PI*180; }
inline real rad(real t) { return t/180*PI; }
struct Sun {
    Sun(float longitude, float latitude) {
        // Orbital parameters
        real Ms = 1.9891e30; //Mass of the sun [kg]
        real m = 5.9736e24; //Mass of the earth [kg]
        real a = 1.4960e11; //Semi-major axis [m]
        real e = 0.01671123; // Earth eccentricity
        real Omega = rad(348.74); // Longitude of ascending node
        real omega = rad(114.21); // Argument of perihelion
        real epsilon = rad(23.439); //Obliquity of the ecliptic
        real G = 6.6738e-11; //Gravitationnal constant [N·(m/kg)²]
        real n = sqrt(G*(Ms+m)/(a*a*a)); //Mean motion [rad/s]

        // J2000 Phases
        real U = currentTime(); //Unix time
        real T = U - 10957.5*24*60*60; // J2000 time [s]
        real M0 = rad(357.51716); //Mean anomaly at J2000
        real GMST0 = 18.697; //J2000 hour angle of the vernal equinox [h]
        real ratio = 1+24*60*60/(2*PI/n); //Sidereal day per solar day
        real GMST = GMST0 + ratio*T/60/60; // Greenwich mean sidereal time

        real M = M0 + n*T; //Mean anomaly
        real vu = M + 2*e*sin(M) + 5/4*e*e*sin(2*M); //True anomaly
        real l = vu + Omega + omega; //Ecliptic longitude (Omega + omega = Longitude of the periapsis)
        real lambda = PI + l; //Ecliptic longitude (geocentric)
        real alpha = atan(cos(epsilon)*sin(lambda), cos(epsilon)*cos(lambda)); //Right ascension
        real delta = asin(sin(epsilon)*sin(lambda)); //Declination
        real h = GMST - (longitude + alpha)/PI*12; // Solar hour angle [h]
        real w0 = acos(-tan(latitude)*tan(delta))/PI*12*60*60; // Half day length [s]
        real noon = U-mod(h,24)*60*60; // Solar noon in unix time [s]
        log(str(Date(noon-w0), "hh:mm"_),str(Date(noon), "hh:mm"_),str(Date(noon+w0), "hh:mm"_));
    }
} test(-2.1875*PI/180, 48.6993*PI/180); //Orsay, France
#endif
