#include "parser.h"
#include "process.h"
struct Calculator {
    Calculator() {
        Parser parser;
        /// Semantic actions used to synthesize attributes
        typedef double real;
        parser["number"_]=  [](ref<byte> token){ return toDecimal(token); };
        parser["e"_]=  [](real a, real b){ return a*exp10(b); };
        parser["add"_] =  [](real a, real b){ return a+b; };
        parser["sub"_] = [](real a, real b){ return a-b; };
        parser["mul"_] = [](real a, real b){ return a*b; };
        parser["div"_] =  [](real a, real b){ return a/b; };
        // S-attributed EBNF grammar for arithmetic expressions
        parser.generate(
                    R"(
                    Expr: Term | Expr '+' Term { value: add Expr.value Term.value } | Expr '-' Term { value: sub Expr.value Term.value }
                    Term: Factor | Term '*' Factor { value: mul Term.value Factor.value } | Term '/' Factor { value: div Term.value Factor.value }
                    Factor: Number | Number 'e' Number { value: e Number[0].value Number[1].value } | '(' Expr ')'
                    //Factor: Number | '(' Expr ')'
                    Number: [0-9]+ ('.' [0-9]+)? { value: number }
                                                 )"_);
                const ref<byte>& input =  "3.14e2"_;
        Node result = parser.parse(input);
        log(input,"=",result.values.at("value"_));
    }
} test; 
