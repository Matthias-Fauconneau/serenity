#include "process.h" // Mandatory module for an application main file (provides main entry point for C runtime)
#include "string.h" // Message formatting (i.e concatenation and automatic type conversion to string) is implemented by the variadic template function str(Args...)
#include "window.h" //Window
#include "text.h" //TextInput
#include "time.h" //Date
#include "parser.h"

/// Demonstrates basic usage of Serenity C++ framework
struct Examples {
    Examples() {
#if 0
        /// 1) Outputs a message to standard output
        log("Hello World!");

        /// 2) Outputs sizes of integer and floating-point types
        // C types: size are not explicit in the name, signedness of char depends on implementation, long is platform dependent (size of a pointer on linux, always 32bit on windows)
        log("char",sizeof(char),"short",sizeof(short),"int",sizeof(int),"long",sizeof(long),"long long",sizeof(long long)); // unsigned types prefixed with "unsigned "
        // core.h provides types with explicit size
        log("int8",sizeof(int8),"int16",sizeof(int16),"int32",sizeof(int32),"int64",sizeof(int64)); // unsigned with u (e.g uint8)
        log("float",sizeof(float),"double",sizeof(double),"long double",sizeof(long double));
#endif

        /// Shows a window with a text input field
        TextInput input; focus=&input; Window window(&input, int2(640,480), "Input"_);
        window.localShortcut(Escape).connect(&exit);
#if 0
        /// 3) Inputs one ASCII character and outputs the character in uppercase
        char c=0; // this variable will contain the input character
        window.setTitle("Type a letter");
        input.textChanged.connect([&c](const ref<byte>& text) { if(text) c=text[0]; });
        while(c<'a' || c>'z') if(mainThread().processEvents()) return; // Synchronously process events (would run in main in a normal application)
        char u=c+'A'-'a'; // Converts to uppercase
        log(u);

        input.text.clear(); input.textChanged.clear();

        /// 4) Inputs a date and outputs the date + 30min
        Date date; // this variable will contain the date
        window.setTitle("Type a date"_);
        input.textChanged.connect([&date](const ref<byte>& text) { date=parse(text); log(text,date); });
        while(date.hours<0 || date.minutes<0) if(mainThread().processEvents()) return;
        log(Date(date+30*60));

        input.text.clear(); input.textChanged.clear();

        /// 5) Fills an array with integers from 9 to 0
        array<int> a;
        for(int i: range(10)) a<< 9-i;
        log(a);

        /// 6) Computes an integer arithmetic expression (e.g "(10+(21-32)*43)/54" )
        Parser parser;
        /// Semantic actions used to synthesize attributes
        parser["integer"_]=  (function<int(ref<byte>)>) [](ref<byte> token){ return toInteger(token); };
        parser["add"_] = (function<int(int,int)>) [](int a, int b){ return a+b; };
        parser["sub"_] = (function<int(int,int)>) [](int a, int b){ return a-b; };
        parser["mul"_] = (function<int(int,int)>) [](int a, int b){ return a*b; };
        parser["div"_] = (function<int(int,int)>) [](int a, int b){ return a/b; };
        parser.generate( readFile("serenity/math.g"_) ); // S-attributed EBNF grammar for arithmetic expressions
        // Contents of math.g:
        // Expr: Term | Expr '+' Term { value: add Expr.value Term.value } | Expr '-' Term { value: sub Expr.value Term.value }
        // Term: Factor | Term '*' Factor { value: mul Term.value Factor.value } | Term '/' Factor { value: div Term.value Factor.value }
        // Factor: [0-9]+ { value: integer } | '(' Expr ')'
        window.setTitle("Type an expression and press enter"_);
        ref<byte> expr;
        input.textEntered.connect([&expr](const ref<byte>& text) { expr=text; });
        while(!expr) if(mainThread().processEvents()) return;
        Node result = parser.parse(expr);
        log(result.values.at("value"_));

        input.text.clear(); input.textEntered.clear();
#endif

        /// 7) Machine epsilon
        // single precision
        float e = 1.f;
        log(bin((int&)e,32)); // exponent=0 [encoded as 127], significand = <implicit 1>
        (int&)e += 1; // Increments the least significant digit (adds one unit in the last place)
        log(bin((int&)e,32)); // significand = <implicit 1>·2^0 + 1·2^-23
        e -= 1.f; // Substracts the initial one (scales the exponent to shift the epsilon into the implicit bit)
        log(bin((int&)e,32)); // exponent= -23 [104], significand = <implicit 1>
        log(e); //2^-23
        // double precision
        double e64 = 1.0;
        (int&)e64 += 1;
        log(e64-1.0); //2^-52
        // legacy x87 extended precision
        long double e80 = 1;
        (int&)e80 += 1;
        log<double>(e80-1); //2^-63

        /// 8) Random number binary search game

        /// Generates a sequence of pseudo-random 64bit integers
        struct Random {
            uint64 sz,sw;
            uint64 z,w;
            Random() { seed(); reset(); }
            void seed() { sz=rdtsc(); sw=rdtsc(); }
            void reset() { z=sz; w=sw; }
            uint64 operator()() {
                z = 36969 * (z & 0xFFFF) + (z >> 16);
                w = 18000 * (w & 0xFFFF) + (w >> 16);
                return (z << 16) + w;
            }
        } rand;

        int number = rand()%11;
        window.setTitle("Guess a number between 0 and 10"_);
        int guess=-1;
        input.textChanged.connect([&guess](const ref<byte>& text) { if(text) guess=toInteger(text); });
        while(guess != number) {
            if(mainThread().processEvents()) return;
            if(guess<0) continue;
            /**/  if(guess>number) window.setTitle("Less"_);
            else if(guess<number) window.setTitle("More"_);
        }

        //4.1, 4.3 à 4.5, 5.1 à 5.3, 5.5, 6.1, 6.2, 6.4, 7.1, 7.2, 7.3, 7.5, 7.6
    }
} application; //global variables are registered for construction by the compiler before entering main()
