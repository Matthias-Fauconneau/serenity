#include "parser.h"
#include "process.h"
struct Calculator {
    Calculator() {
        Parser parser;
        /// Semantic actions used to synthesize attributes
        typedef double real;
        parser["number"_]=  [](ref<byte> token){ return toDecimal(token); };
        parser["add"_] =  [](real a, real b){ return a+b; };
        parser["sub"_] = [](real a, real b){ return a-b; };
        parser["mul"_] = [](real a, real b){ return a*b; };
        parser["div"_] =  [](real a, real b){ return a/b; };
        parser.generate( // S-attributed EBNF grammar for arithmetic expressions
                         "Expr: Term | Expr '+' Term { value: add Expr.value Term.value } | Expr '-' Term { value: sub Expr.value Term.value }""\n"
                         "Term: Factor | Term '*' Factor { value: mul Term.value Factor.value } | Term '/' Factor { value: div Term.value Factor.value }""\n"
                         "Factor: [0-9]+ ('.' [0-9]+)? { value: number } | '(' Expr ')'""\n"_);
        const ref<byte>& input =  "(10.5+(21-32)*43)/54"_;
        Node result = parser.parse(input);
        log(input,"=",result.values.at("value"_));
    }
} test; 
