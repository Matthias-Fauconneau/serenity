#include "process.h"
#include "string.h"
#include "time.h"
#include "parser.h"

// Generates a sequence of uniformly distributed pseudo-random 64bit integers
struct Random {
    uint64 sz,sw;
    uint64 z,w;
    Random() { seed(); reset(); }
    void seed() { sz=rdtsc(); sw=rdtsc(); }
    void reset() { z=sz; w=sw; }
    uint64 next() {
        z = 36969 * (z & 0xFFFF) + (z >> 16);
        w = 18000 * (w & 0xFFFF) + (w >> 16);
        return (z << 16) + w;
    }
    uint64 operator()() { return next(); }
    operator float() { float f = float(next()&((1<<24)-1))*0x1p-23f - 1; assert(f>=-1 && f<=1); return f; }
} random;

struct Examples {
    Examples() {
        {log("== Sizeof ==");
            log("char",sizeof(char),"short",sizeof(short),"int",sizeof(int),"long",sizeof(long),"long long",sizeof(long long));
            log("int8",sizeof(int8),"int16",sizeof(int16),"int32",sizeof(int32),"int64",sizeof(int64));
            log("float",sizeof(float),"double",sizeof(double),"long double",sizeof(long double));
        }

        {log("== Uppercase ==");
            char c='q';
            char u=c+'A'-'a'; // Converts an ASCII lowercase letter to uppercase
            log(c,"-^>",u);
        }

        {log("== Date ==");
            Date date = parse("23h30"_);
            log(date,"+ 0h30 =",Date(date+30*60));
        }

        {log("== Arithmetic expression ==");
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

        {log("== Machine epsilon ==");
            // single precision
            float e = 1.f;
            log("1.f                  \t",bin((int&)e,32)); // exponent=0 [encoded as 127], significand = <implicit 1>
            (int&)e += 1; // Increments the least significant digit (adds one unit in the last place)
            log("1.f + 1ulp       \t",bin((int&)e,32)); // significand = <implicit 1>·2^0 + 1·2^-23
            e -= 1.f; // Substracts the initial one (scales the exponent to shift the epsilon into the implicit bit)
            log("1.f + 1ulp - 1.f\t",bin((int&)e,32)); // exponent= -23 [104], significand = <implicit 1>
            log(ftoa(e,2,2)); //2^-23
            // double precision
            double e64 = 1.0;
            (int&)e64 += 1;
            log(ftoa(e64-1.0,2,2)); //2^-52
            // legacy x87 extended precision
            long double e80 = 1;
            (int&)e80 += 1;
            log(ftoa(e80-1,2,2)); //2^-63
        }

        {log("== Random number binary search game ==");
            int number = random()%11;
            for(int min=0,max=11;;) {
                int guess = min+random()%(max-min); // Emulates user guesses
                /**/  if(guess>number) max=guess, log(guess, "High!"_);
                else if(guess<number) min=guess, log(guess, "Low!"_);
                else { log(guess,"Got it!"); break; }
            };
        }

        {log("== Pascal's triangle ==");
            constexpr int N = 10;
            struct { int buffer[N*N]={}; int& operator()(uint n, uint p){ return buffer[n*N+p]; } } C;
            string triangle;
            for(int n: range(N)) {
                for(int p: range(n+1)) {
                    C(p,n) = n>0 && p>0 && p<n ? C(p-1,n-1)+C(p,n-1) : 1;
                    triangle<< str(C(p,n)) <<'\t';
                }
                triangle<<'\n';
            }
            log(triangle);
        }

        {log("== Binary search ==");
            writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"performance"_);
            constexpr int N = 22;
            float linear[N], binary[N];
            for(int n: range(1,N+1)) {
                array<uint64> haystack;
                for(int unused i: range(n)) haystack.insertSorted(random()); // Generates an array of sorted random integers (using insertion sort)
                uint64 linearTime=0, binaryTime=0;
                int times = 1<<16;
                for(int unused i: range(times)) { // Tests several times both methods
                    int index = random()%haystack.size();
                    uint64 needle = haystack[index];
                    assert(haystack.indexOf(needle)==index);
                    assert(haystack.binarySearch(needle)==index);
                    yield();
                    linearTime += cycles(volatile unused int i = haystack.indexOf(needle));
                    binaryTime += cycles(volatile unused int i = haystack.binarySearch(needle));
                }
                linear[n-1] = float(linearTime)/times/n;
                binary[n-1] = float(binaryTime)/times/n;
                // Results: (tested with clang on K8)
                // linear search is faster until 22 elements (~ 8 cycles / 3 elements)
                // binary search is twice faster for 86 (linear approaches 8 cycles / 5 elements)
            }
            writeFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"_,"conservative"_);
            for(int n: range(1,N+1)) log(n,"\tlinear",linear[n-1],"cycles/element","\tbinary",binary[n-1],"cycles/element","\tratio",linear[n-1]/binary[n-1]);
        }

        {log("== Statistics ==");
            // Generates a sequence of normally distributed pseudo-random floats
            struct NormalDistribution {
                float mean=0, deviation=1;
                float next() {
                    float x,y,r;
                    do { x = random, y = random; r = x*x + y*y; } while(r>=1 || r==0);
                    return mean+deviation*x*__builtin_sqrtf(-2*__builtin_logf(r)/r);
                }
                operator float() { return next(); }
            } normal;
            // Computes mean and variance
            float X=0,X2=0; // of a normal distribution of mean=0
            float Y=0,Y2=0; // of the same normal distribution shifted by 1
            { // Computing variance in one pass using <x^2> - <x>^2 looses precision (ill-conditioned when mean is further from 0)
                constexpr int N = 10000000;
                for(int unused i: range(N)) {
                    float x = normal;
                    X += x;
                    X2 += x*x;
                    float y = x+1; // Same distribution with mean=1
                    Y += y;
                    Y2 += y*y;
                }
                X /= N, X2 /= N, Y /= N, Y2 /= N;
            }
            log("μ0",ftoa(X,4),"σ0",ftoa(X2-X,4),"μ1",ftoa(Y,4),"σ1",ftoa(Y2-Y,4));
        }

        {log("== Factorial ==");
            constexpr int N = 21;
            for(int n: range(N)) {
                uint64 f=1; for(int i: range(n)) f *= i+1;
                const double PI = 3.14159265359, e = 2.71828182846;
                float s = __builtin_sqrt(2*PI*n)*__builtin_pow(n/e,n);
                log(n,"\tε = ",ftoa((f-s)/s,0,10), "\tn! = ",f, "\t√(2πn)·(n/e)^n = ",s); //20 2432902008176640000 2422786791066042368.00 4·10^-3
            }
        }

        {log("== Fibonacci ==");
            //uint64 fibonacci(uint64 n) { return n<=1 ? n : fib(n-1) + fib(n-2); } // Exponential time, linear space
            constexpr int N = 13;
            string s;
            for(int n: range(N)) {
                uint64 f0=0, f1=1; for(;n>=1;n--) { uint64 t=f0; f0=f1, f1+=t; } // Linear time, constant space
                s<< str(f0,' ');
            }
            log(s);
        }

        {log("== Hanoi ==");
            struct Hanoi {
                int count=0;
                Hanoi(int n) { hanoi(n,1,2,3); }
                void hanoi(int n, int A, int B, int C) { // Exponential time O(2^n), linear space
                    if(n>1) hanoi(n-1,A,C,B);
                    count++; //log("move disc",n,"from",A," to",C);
                    if(n>1) hanoi(n-1,B,A,C);
                }
            };
            constexpr int N = 17;
            for(int n: range(N)) {
                log(n,"\t",Hanoi(n).count,"moves");
            }
        }
    }
} application;
