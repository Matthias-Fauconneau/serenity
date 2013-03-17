#if 0
#include <type_traits>
#include <utility>
#include <iostream>
template<typename... Args> struct function;
template<typename... Args> struct function {
    void operator()(Args&&...) const { std::cout<<sizeof...(Args); }
};

struct FunctionBase { virtual void invoke() const=0; };

template<typename F> struct DynamicFunction;
template<typename O, typename T, typename... Params> struct DynamicFunction<T (O::*)(Params...) const> : FunctionBase {
    function<Params...> f;
    template<typename Arg, typename... RemainingArgs, typename... Args> void invoke(Args... args) const {
        typename std::remove_reference<Arg>::type arg;
        invoke<RemainingArgs...>(args..., arg);
    }
    template<typename... RemainingArgs> void invoke(Params... args) const { f(std::forward<Params>(args)...); } //SFINAE Args==Params
    void invoke() const override { invoke<Params...>(); }
};
template<typename F> void test_virtual(F) { DynamicFunction<decltype(&F::operator())>().invoke(); }

template<typename F> struct StaticFunction;
template<typename O, typename T, typename... Params> struct StaticFunction<T (O::*)(Params...) const> {
    function<Params...> f;
    template<typename Arg, typename... RemainingArgs, typename... Args> void invoke(Args... args) const {
        typename std::remove_reference<Arg>::type arg;
        invoke<RemainingArgs...>(args..., arg);
    }
    template<typename... RemainingArgs> void invoke(Params... args) const { f(std::forward<Params>(args)...); } //SFINAE Args==Params
    void invoke() const { invoke<Params...>(); }
};
template<typename F> void test_static(F) { StaticFunction<decltype(&F::operator())>().invoke(); }

struct type {};
int main() {
    auto one_const_argument = [](const type& a)->type{ return a; };
    test_static( one_const_argument ); // Succeed
    test_virtual( one_const_argument ); // Succeed

    auto two_value_arguments = [](type a, type)->type{ return a; };
    test_static( two_value_arguments ); // Succeed
    test_virtual( two_value_arguments ); // Succeed

    auto two_ref_arguments = [](const type& a, const type&)->type{ return a; };
    test_static( two_ref_arguments ); // Succeed
    test_virtual( two_ref_arguments ); // Fails
    return 0;
}
#else
#include "parser.h"
#include "process.h"

typedef double real;
typedef ref<byte> Name;

static map<Name, map<Name, int>> dimensions = {
{"1"_,{}}, // Dimensionless
// Base quantities
{"length"_,{{"L"_,1}}},
{"time"_,{{"T"_,1}}},
{"mass"_,{{"M"_,1}}},
{"temperature"_,{{"Θ"_,1}}},
// Derived quantities
{"frequency"_,{{"T"_,-1}}}
        };

struct ReferenceUnit { Name name; Name dimension; real magnitude; };
bool operator ==(const ReferenceUnit& a, const ReferenceUnit& b) { return a.name == b.name; }
template<> string str(const ReferenceUnit& o) { return string(o.name); }

static ref<ReferenceUnit> units = {{"s"_,"time"_,1},{"min"_,"time"_,60},{"h"_,"time"_,60*60}};

struct Unit {
    map<ReferenceUnit, int> base; // Set of units composing this one
    /// Returns the combined magnitude of this unit (over SI)
    real magnitude() const {
        real magnitude = 1;
        for(auto unit: base) magnitude *= pow(unit.key.magnitude, unit.value);
        return magnitude;
    }
    /// Returns the decomposition of this unit on the canonical basic dimensions (L,T,M,Θ)
    map<Name, int> canonical() const {
        map<Name, int> base;
        for(auto unit: this->base) for(auto dimension: dimensions[unit.key.dimension]) base[dimension.key] += unit.value*dimension.value;
        return base;
    }
};
Unit copy(const Unit& o) { return {copy(o.base)}; }
bool check(const Unit& a, const Unit& b) {
    if(a.canonical() != b.canonical()) { error("Incomparable quantities", a, b); return false; }//TODO: compare decomposition into base quantities
    return true;
}
template<> string str(const Unit& o) {
    string s;
    for(auto unit: o.base) { //TODO: recompose derived units
        assert(unit.value!=0);
        if(unit.value==-1) s<<'/'<<unit.key.name;
        else { s<<unit.key.name; if(unit.value!=1) s<<str(unit.value); }
    }
    return s;
}
Unit operator*(const Unit& a, const Unit& b) {
    Unit p = copy(a);
    for(auto unit: b.base) p.base[unit.key] += unit.value;
    return p;
}
Unit operator/(const Unit& a, const Unit& b) {
    Unit p = copy(a);
    for(auto unit: b.base) p.base[unit.key] -= unit.value;
    return p;
}
/// Returns the ratio of the magnitude of a over the magnitude of b for comparable quantities
real magnitude(const Unit& a, const Unit& b) {
    assert(check(a,b));
    return a.magnitude()/b.magnitude();
}

struct Quantity { real value; Unit unit; };
template<> string str(const Quantity& o) { return str(o.value,o.unit); }
Quantity operator+(const Quantity& a, const Quantity& b) { return {a.value*magnitude(a.unit,b.unit)+b.value, copy(b.unit)}; }
Quantity operator-(const Quantity& a, const Quantity& b) { return {a.value*magnitude(a.unit,b.unit)-b.value, copy(b.unit)}; }
Quantity operator*(const Quantity& a, const Quantity& b) { return {a.value*b.value,a.unit*b.unit}; }
Quantity operator/(const Quantity& a, const Quantity& b) { return {a.value/b.value,a.unit/b.unit}; }

struct Calculator {
    Calculator() {
        Parser parser;
        /// Semantic actions used to synthesize attributes
        parser["decimal"_]=  [](ref<byte> token)->real{ return toDecimal(token); };
        parser["e"_]=  [](real a, real b)->real{ return a*exp10(b); };
        parser["p"_]=  [](real a, real b)->real{ return a*exp2(b); };
        parser["add"_] =  [](const Quantity& a, const Quantity& b)->Quantity{ return a+b; };
        parser["sub"_] = [](const Quantity& a, const Quantity& b)->Quantity{ return a-b; };
        parser["mul"_] = [](const Quantity& a, const Quantity& b)->Quantity{ return a*b; };
        parser["div"_] =  [](const Quantity& a, const Quantity& b)->Quantity{ return a/b; };
        parser["unit"_]=  [](ref<byte> name)->Unit{
            for(const ReferenceUnit& unit: units) if(unit.name==name) return Unit{{{unit,1}}};
           error("Unknown unit", name);
            return {{{{name,"1"_,1},1}}}; //Create a new dimensionless unit
        };
        parser["dimensionless"_]=[](real value)->Quantity{ return {value}; };
        parser["quantity"_]=[](real value, const Unit& unit)->Quantity{ return {value, copy(unit)}; };
        parser["convert"_]=  [](const Quantity& from, const Unit& to)->Quantity{
            return {from.value*magnitude(from.unit,to), copy(to)};
        };
        // S-attributed EBNF grammar for quantity calculus
        //TODO: for(units) rules << {name, {unit: []{ return unit }}};
        parser.generate( //TODO: own file
                    R"(
                    S: Expr
                      | Expr " in " Unit {quantity: convert Expr.quantity Unit.unit}
                    Expr: Term
                          | Expr '+' Term {quantity: add Expr.quantity Term.quantity}
                          | Expr '-' Term {quantity: sub Expr.quantity Term.quantity}
                    Term: Factor
                          | Term '*' Factor {quantity: mul Term.quantity Factor.quantity}
                          | Term '/' Factor {quantity: div Term.quantity Factor.quantity}
                    Factor: Number {quantity: dimensionless Number.value}
                              | Number Unit {quantity: quantity Number.value Unit.unit}
                              | '(' Expr ')'
                    Number: Decimal
                                | Decimal 'e' Integer {value: e Decimal.value Integer.value}
                                | Decimal 'p' Integer {value: p Decimal.value Integer.value}
                    Decimal: '-'? [0-9]+ ('.' [0-9]+)? {value: decimal}
                    Integer:  '-'? [0-9]+ {value: decimal}
                    Unit: ("h"|"min") {unit: unit}
                          #| Unit Unit {unit: product Unit[0].unit Unit[1].unit}
                          #| Unit '/' Unit {unit: per Unit[0].unit Unit[1].unit}
                                                      )"_);
                const ref<byte>& input =  "3h in min"_;
        Node result = parser.parse(input);
        log(input,"=",result.values.at("quantity"_));
    }
} test; 
#endif
