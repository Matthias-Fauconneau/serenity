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
inline double mod(double q, double d) { return __builtin_fmod(q, d); }
const double PI = 3.14159265358979323846;
inline double cos(double t) { return __builtin_cos(t); }
inline double sin(double t) { return __builtin_sin(t); }
inline double asin(double t) { return __builtin_asin(t); }
inline double atan(double t) { return __builtin_atan(t); }
inline double atan(double y, double x) { return __builtin_atan2(y, x); }
string hours(float hours) { return dec(int(hours)%24,2)+":"_+dec(int(abs(hours)*60)%60,2); }
string seconds(float seconds) { return dec(int(seconds/60/60)%24,2)+":"_+dec(int(abs(seconds/60))%60,2); }
struct Sun {
    Sun() {
        double U = currentTime();
        double JD = 2440587.5 + U/60/60/24;
        double D = JD - 2451545.0;
        double L = (280.460 + 0.9856474*D)*PI/180; //mean longitude
        double g = (357.528 + 0.9856003*D)*PI/180; //mean anomaly
        double lambda = L + (1.915*sin(g) + 0.020*sin(2*g))*PI/180; //ecliptic longitude
        log("ecliptic longitude",mod(lambda*180/PI,360));
        double e = (23.439-0.0000004*D)*PI/180; //obliquity of the ecliptic
        double alpha = atan(cos(e)*sin(lambda), cos(e)*cos(lambda)); //right ascension
        double delta = asin( sin(e)*sin(lambda) ); //declination (-2.6)
        log("obliquity",mod(e*180/PI,360),"ascension", 360+alpha*180/PI, "declination", delta*180/PI);
        //for Orsay, France
        double lambda0 = -2.1875*PI/180; //longitude (positive west)
        double phi = 48.6993*PI/180; //latitude
        double GMST = mod(18.6973 + 24.06570*D, 24); //Greenwich mean sidereal time (hours)
        double h = GMST*PI/12 - lambda0 - alpha; //hour angle (time since meridian)
        log("hour angle", h*180/PI, hours(h*12/PI));
        double A = atan( sin(h), cos(h)*sin(phi) - sin(e)*sin(lambda)*cos(phi)); //azimuth (measured from south, turning positive to the West)
        double a = asin( sin(phi)*sin(delta) + cos(phi)*cos(delta)*cos(h) ); //altitude
        log("A", 180+A*180/PI, "a", a*180/PI);
    }
} test;
#endif
