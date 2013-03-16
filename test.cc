#include "parser.h"
#include "process.h"

typedef double real;
typedef ref<byte> Name;

static map<Name, map<Name, int>> dimensions = {
{"1"_,{}},
{"length"_,{{"length"_,1}}},
{"time"_,{{"time"_,1}}},
{"mass"_,{{"mass"_,1}}},
{"temperature"_,{{"temperature"_,1}}},
        };

struct Unit { Name name; Name quantity; real magnitude; };
template<> string str(const Unit& unit) { return string(unit.name); }
static ref<Unit> units = {{"s"_,"time"_,1},{"min"_,"time"_,60},{"h"_,"time"_,60*60}};

struct Calculator {
    Calculator() {
        Parser parser;
        /// Semantic actions used to synthesize attributes
        parser["number"_]=  [](ref<byte> token){ return toDecimal(token); };
        parser["e"_]=  [](real a, real b){ return a*exp10(b); };
        parser["p"_]=  [](real a, real b){ return a*exp2(b); };
        parser["add"_] =  [](real a, real b){ return a+b; };
        parser["sub"_] = [](real a, real b){ return a-b; };
        parser["mul"_] = [](real a, real b){ return a*b; };
        parser["div"_] =  [](real a, real b){ return a/b; };
        parser["unit"_]=  [](ref<byte> name) -> Unit {
            for(const Unit& unit: units) if(unit.name==name) return unit;
            return Unit{name,"1"_,1};
        };
        parser["convert"_]=  [](real value, Unit from, Unit to){
            if(dimensions[from.quantity] != dimensions[to.quantity]) error("Incompatible quantities");
            return value*from.magnitude/to.magnitude;
        };
        // S-attributed EBNF grammar for arithmetic expressions (and unit conversions)
        parser.generate(
                    R"(
                    S: Expr | Expr " in " Unit {value: convert Expr.value Expr.unit Unit.unit} {unit: Unit.unit}
                    Expr: Term | Expr '+' Term {value: add Expr.value Term.value} | Expr '-' Term {value: sub Expr.value Term.value}
                    Term: Factor | Term '*' Factor {value: mul Term.value Factor.value} | Term '/' Factor {value: div Term.value Factor.value}
                    Factor: Number | Number Unit {value: Number.value} {unit: Unit.unit}
                              | '(' Expr ')'
                              | Number 'e' Number {value: e Number[0].value Number[1].value}
                              | Number 'p' Number {value: p Number[0].value Number[1].value}
                    Number: '-'? [0-9]+ ('.' [0-9]+)? {value: number}
                    Unit: ("h"|"min") {unit: unit}
                                                      )"_);
                const ref<byte>& input =  "3h in min"_;
        Node result = parser.parse(input);
        log(input,"=",result.values.at("value"_),result.values.at("unit"_));
    }
} test; 
