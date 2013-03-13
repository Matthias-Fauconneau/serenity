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
inline double tan(double t) { return __builtin_tan(t); }
inline double acos(double t) { return __builtin_acos(t); }
inline double asin(double t) { return __builtin_asin(t); }
inline double atan(double t) { return __builtin_atan(t); }
inline double atan(double y, double x) { return __builtin_atan2(y, x); }
string hours(double hours) { return (hours<0?"-"_:""_)+dec(mod(abs(hours),24),2)+":"_+dec(mod(abs(hours)*60,60),2); }
string seconds(double seconds) { return dec(int(seconds/60/60)%24,2)+":"_+dec(int(abs(seconds/60))%60,2); }
struct Sun {
    Sun(float longitude /*positive west*/, float latitude) {
        double U = currentTime(); //Unix time
        double D = U/60/60/24 - 10957.5; //J2000 day
        double L = (280.460 + 0.9856474*D)*PI/180; //mean longitude
        double g = (357.528 + 0.9856003*D)*PI/180; //mean anomaly
        double lambda = L + (1.915*sin(g) + 0.020*sin(2*g))*PI/180; //ecliptic longitude
        double e = (23.439-0.0000004*D)*PI/180; //obliquity of the ecliptic
        double alpha = atan(cos(e)*sin(lambda), cos(e)*cos(lambda)); //right ascension
        double delta = asin(sin(e)*sin(lambda)); //declination
        double phi = latitude*PI/180; //latitude
        double GMST = 18.6973 + 24.06570*D; //Greenwich mean sidereal time (hours)
        double h = GMST*PI/12 - longitude*PI/180 - alpha; //hour angle (time since meridian)
        double noon = U-mod(h,2*PI)*12*60*60/PI; // h = 0
        float w0 = acos(-tan(phi)*tan(delta))/PI*12*60*60;
        log(str(Date(noon-w0), "hh:mm"_),str(Date(noon), "hh:mm"_),str(Date(noon+w0), "hh:mm"_));
        double A = atan( sin(h), cos(h)*sin(phi) - sin(e)*sin(lambda)*cos(phi)); //azimuth
        double a = asin( sin(phi)*sin(delta) + cos(phi)*cos(delta)*cos(h) ); //altitude
        log("A", 180+A*180/PI, "a", a*180/PI);
    }
} test(-2.1875, 48.6993); //Orsay, France
#endif
