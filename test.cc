#include "process.h"
#include "string.h"
#include "display.h"

struct Test {
    typedef double real;
    /// Maps [0,1] to [red,blue] black real radiation
    byte4 positiveToColor(real x) {
        constexpr real red=0.700, green=0.546, blue=0.436, C = 2.82;
        constexpr real Cr=1/red, Cg=1/green, Cb=1/blue;
        constexpr real Tr=1/(C*red), Tg=1/(C*green), Tb=1/(C*blue);
        //constexpr real Tm=1/(C*3000), TM=1/(C*200);
        constexpr real Tm=Tr, TM=Tb;
        real T = Tm+x*(TM-Tm);
        const real Ir = Tr*Tr*Tr/(exp(Cr/T)-1);
        const real Ig = Tg*Tg*Tg/(exp(Cg/T)-1);
        const real Ib = Tb*Tb*Tb/(exp(Cb/T)-1);
        log(T, Ir, Ig, Ib, Cr/T, Cg/T, Cb/T, Ib/Ir);
        /*const real Imin = min(min(Ir,Ig),Ib), Imax = max(max(Ir,Ig),Ib); // saturate
        return byte4(sRGB((Ib-Imin)/(Imax-Imin)),sRGB((Ig-Imin)/(Imax-Imin)),sRGB((Ir-Imin)/(Imax-Imin)), 0xFF);*/
        const real Imax = 0.8; //max(max(Ir,Ig),Ib);
        return byte4(sRGB(Ib/Imax),sRGB(Ig/Imax),sRGB(Ir/Imax), 0xFF);
    }
    Test() {
        const real red=700, green=546, blue=436, C = 2.82;
        constexpr real Cr=1/red, Cg=1/green, Cb=1/blue;
        log(Cr, Cg, Cb);
        const real Tr=1/(C*red), Tg=1/(C*green), Tb=1/(C*blue);
        log("red",Tr*1e3,"K","green",Tg*1e3,"K", "blue",Tb*1e3,"K");
        const real h = 6.63e-34 /*J.s*/, k = 1.38e-23 /*J/K*/, c=3e8 /*m/s*/;
        // λ = (hc/kx) / T
        //log(ftoa((h*c/k/C) / 1700 * 1e9,2,0,true), ftoa((h*c/k/C) / 27000 * 1e9,2,0,true));
        log((h*c/k/C) / 1700 * 1e9, (h*c/k/C) / 27000 * 1e9);
        log(positiveToColor(0));
        log(positiveToColor(1));
    }
} test;

#if 0
#include "parser.h"
#include "process.h"

struct EarleyTest {
    EarleyTest() {
        Parser parser;
        /*parser.generate(
                    R"(
                    Term: Factor
                          | Term '/' Factor
                    Factor: 'x'
                              | 'x' UnitExpr
                    UnitExpr: 'u'
                                 | 'u' '/' 'u'
                    )"_);
        const ref<byte>& input =  "xu/u"_;*/

        parser.generate(
                            R"(
                            S: M
                              | S '+' M
                            M: T
                              | M '*' T
                            T: '1'|'2'|'3'|'4'
                            )"_);
        const ref<byte>& input =  "2+3*4"_;
        parser.parse(input);
    }
} test;
#endif

