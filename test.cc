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
inline double mod(double q, double d) { return __builtin_modf(q, &d); }
const double PI = 3.14159265358979323846;
inline double cos(double t) { return __builtin_cos(t); }
inline double sin(double t) { return __builtin_sin(t); }
inline double asin(double t) { return __builtin_asin(t); }
inline double atan(double t) { return __builtin_atan(t); }
inline double atan(double x, double y) { return __builtin_atan2(y, x); }
struct Sun {
    Sun() {
        float D = (float(currentTime())/60/60-12)/24 - 30*365.25; //days since GMT noon on 1 January 2000
        float L = 280.460 + 0.9856474*D; L=mod(L*PI/180,2*PI); //mean longitude
        float g = 357.528 + 0.9856003*D; g=mod(g*PI/180,2*PI); //mean anomaly
        float l = L + 1.915*sin(g) + 0.020*sin(2*g); //ecliptic longitude
        float e = (23.439-0.0000004*D)*PI/180; //obliquity of the ecliptic
        float alpha = atan(cos(e)*cos(l), cos(e)*sin(l)); //right ascension
        float d = atan(sin(e)*sin(l)); //declination
        //for Orsay, France
        float lambda = 2.1875*PI/180; //longitude (positive east)
        float phi = 48.6993*PI/180; //latitude
        float GMST = 18.6973 + 24.06570*D;
        float thetaG = GMST*PI/12;
        float h = thetaG - lambda - alpha; //hour angle
        float sin_a = sin(phi)*sin(d) + cos(phi)*cos(d)*cos(h);
        float a = asin( sin_a  ); //altitude
        float t = a*12/PI; //solar time
        log(dec(int(t/60),2)+":"_+dec(int(t)%60,2));
    }
} test;
#endif
