#include "parser.h"
#include "process.h"
#include "utf8.h"

typedef double real;
typedef ref<byte> Name;

// Dimensions of a quantity
typedef map<Name, int> Dimensions;
map<Name, int> operator*(int n, const map<Name, int>& A) {
    map<Name, int> r; for(auto dimension: A) r[dimension.key]=dimension.value*n; return r;
}
void simplify(Dimensions& A) { for(uint i=0; i<A.size();) if(A.values[i]==0) { A.keys.removeAt(i); A.values.removeAt(i); } else i++; }
void operator+=(map<Name, int>& A, const map<Name, int>& B) { for(auto dimension: B) A[dimension.key]+=dimension.value; simplify(A); }
void operator-=(map<Name, int>& A, const map<Name, int>& B) { for(auto dimension: B) A[dimension.key]-=dimension.value; simplify(A); }
map<Name, int> operator+(const map<Name, int>& A, const map<Name, int>& B) { map<Name, int> r = copy(A); r+=B; return r; }
map<Name, int> operator-(const map<Name, int>& A, const map<Name, int>& B) { map<Name, int> r = copy(A); r-=B; return r; }
/// Returns how many times a B is contained in A
int operator/(const map<Name, int>& A, const map<Name, int>& B) {
    int times = 0;
    for(auto dimension: A) {
        if(!B.contains(dimension.key)) return 0;
        int a = dimension.value, b=B.at(dimension.key);
        int t = a/b; //sign a%b = sign a
        times = times>0 && t > 0 ? min(times,t) : times<0 && t < 0 ? max(times,t) : times == 0 ? t : 0;
    }
    return times;
}

static Dimensions dimensionless;
static map<Name, Dimensions> quantities = {
// Base quantities
{"length"_,{{"L"_,1}}},
{"time"_,{{"T"_,1}}},
{"mass"_,{{"M"_,1}}},
{"temperature"_,{{"Θ"_,1}}},
// Simple derived quantities
{"surface"_,{{"L"_,2}}},
{"volume"_,{{"L"_,3}}},
{"frequency"_,{{"T"_,-1}}}
// TODO: Complex derived quantities
        };

struct ReferenceUnit {
    Name name, quantity; real magnitude;
    const Dimensions& dimensions() const { return quantities.contains(quantity) ? quantities.at(quantity) : dimensionless; }
};
bool operator ==(const ReferenceUnit& a, const ReferenceUnit& b) { return a.name == b.name; }
template<> string str(const ReferenceUnit& o) { return string(o.name); }

static ref<ReferenceUnit> units = {
    // Length
    {"m"_,"length"_,1},
    {"L"_,"volume"_,1e-3},
    // Time
    {"s"_,"time"_,1},{"min"_,"time"_,60},{"h"_,"time"_,60*60},{"days"_,"time"_,24*60*60},
    {"weeks"_,"time"_,7*24*60*60},{"years"_,"time"_,365.25*24*60*60},
    {"Hz"_,"frequency"_,1},
    // Digital storage
    {"b"_,"information"_,1},{"B"_,"information"_,8}
};

struct UnitTerm {
    ReferenceUnit unit; int power; //unit power
    Name prefix; real prefixValue=0; // prefix magnitude modifier
    UnitTerm(ReferenceUnit unit, int power):unit(unit),power(power){}
    real magnitude() const { return pow(unit.magnitude*prefixValue, power); }
    Dimensions dimensions() const { return power*unit.dimensions(); }
};

/// Return all reference units (and power thereof) directly compatible with the dimensions
array<UnitTerm> unitsFor(const Dimensions& dimensions) {
    array<UnitTerm> units;
    for(const ReferenceUnit& unit: ::units) {
        int power = dimensions / unit.dimensions();
        if(power && !(dimensions-power*unit.dimensions())) units << UnitTerm(unit, power);
    }
    assert(units, dimensions);
    return units;
}

/// Complex unit composed of reference units
struct Unit {
    array<UnitTerm> units;
    /// Returns the combined magnitude of this unit (over SI)
    real magnitude() const {
        real magnitude = 1;
        for(const UnitTerm& unit: units) magnitude *= unit.magnitude();
        return magnitude;
    }
    /// Returns the decomposition of this unit on the canonical basic dimensions (L,T,M,Θ)
    Dimensions dimensions() const { Dimensions r; for(const UnitTerm& unit: units) r += unit.dimensions(); return r; }
    /// Returns the simplification of this unit in a minimal number of reference units
    Unit minimal() const {
        Dimensions dimensions = this->dimensions();
        Unit simple;
        for(int i=quantities.size()-1; i>0 && dimensions; i--) { // Greedily match quantities from most complex to simplest
            Dimensions& quantity = quantities.values[i];
            int times = dimensions/quantity;
            if(!times) continue;
            simple.units << unitsFor(quantity).first();
            dimensions -= times * quantity; //Removes the matched dimensions
        }
        for(UnitTerm unit: units) if(!unit.dimensions()) simple.units << unit; // forward dimensionless quantities
        return simple;
    }
};
Unit copy(const Unit& o) { return {copy(o.units)}; }
template<> string str(const Unit& o) {
    string s;
    for(UnitTerm unit: o.units) {
        assert(unit.power!=0);
        s << unit.prefix << unit.unit.name;
        if(unit.power<0) s << "⁻"_;
        if(unit.power!=1) s << utf8(toUTF32("⁰¹²³⁴⁵⁶⁷⁸⁹"_)[abs(unit.power)]);
    }
    return s;
}
Unit operator*(const Unit& a, const Unit& b) { Unit p = copy(a); p.units << b.units; return p; }
Unit operator/(const Unit& a, const Unit& b) {
    Unit p = copy(a); for(const UnitTerm& unit: b.units) { UnitTerm u=unit; u.power=-u.power; p.units << u; }
    return p;
}
/// Returns the ratio of the magnitude of a over the magnitude of b for comparable quantities
real magnitude(const Unit& a, const Unit& b) {
    if(a.dimensions() != b.dimensions()) error("Incomparable quantities", a, b);
    return a.magnitude()/b.magnitude();
}

struct Quantity { real value; Unit unit; };
template<> string str(const Quantity& o) {
    Unit unit = o.unit.minimal();
    real value = o.value*o.unit.magnitude()/unit.magnitude();
    for(UnitTerm& u: unit.units) {
        if(u.dimensions()) {
            for(const UnitTerm& alt: unitsFor(u.dimensions())) {
                real altValue = value*u.magnitude()/alt.magnitude();
                if( abs(log10(altValue)) < abs(log10(value)) ) u=alt, value=altValue; // Greedily minimize log distance to 1
            }
        } else {
            for(const ReferenceUnit& alt: units) if(alt.quantity == u.unit.quantity) {
                real altValue = value*u.magnitude()/alt.magnitude;
                if( abs(log10(altValue)) < abs(log10(value)) ) u=UnitTerm(alt,1), value=altValue; // Greedily minimize log distance to 1
            }
        }
    }
    int exponent = round(log10(value)/3);
    if(exponent) {
        for(UnitTerm& u: unit.units) if(u.power==1) {
            static ref<ref<byte>> prefixes = {"n"_,"μ"_,"m"_,""_,"K"_,"M"_,"G"_};
            assert(exponent>=-3 && exponent <= 3, value, exponent);
            u.prefix = prefixes[exponent+3];
            value /= exp10(exponent*3);
            break;
        }
    }
    return str(value,unit);
}
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
        parser["dimensionless"_]=[](real value)->Quantity{ return {value}; };
        parser["quantity"_]=[](real value, const Unit& unit)->Quantity{ return {value, copy(unit)}; };
        parser["convert"_]=  [](const Quantity& from, const Unit& to)->Quantity{
            return {from.value*magnitude(from.unit,to), copy(to)};
        };
        parser["unit"_]=  [](ref<byte> name)->Unit{
            for(const ReferenceUnit& unit: units) if(unit.name==name) return Unit{array<UnitTerm>{UnitTerm(unit,1)}};
           error("Unknown unit", name);
           return Unit{array<UnitTerm>({UnitTerm({name,name,1},1)})}; //Create a new dimensionless unit
        };
        parser["unitMul"_] = [](const Unit& a, const Unit& b)->Unit { return a*b; };
        parser["unitDiv"_] = [](const Unit& a, const Unit& b)->Unit { return a/b; };
        parser["prefixUnit"_]=[](const ref<byte>& prefix, const Unit& unit)->Unit {
            Unit u=copy(unit);
            u.units.first().prefix = prefix;
            u.units.first().prefixValue = map<Name, real>{{"n"_,1e-9},{"μ"_,1e-6},{"m"_,1e-3},{"K"_,1e3},{"M"_,1e6},{"G"_,1e9},
                                                                                     {"Ki"_,1024},{"Mi"_,pow(1024,2)},{"Gi"_,1e-9}}.at(prefix);
            return u;
        };
        // S-attributed EBNF grammar for quantity calculus
        //TODO: for(units) rules << {name, {unit: []{ return unit }}};
        parser.generate( //TODO: own file
                    R"(
                    S: Expr
                      | Expr " in " UnitExpr {quantity: convert Expr.quantity UnitExpr.unit}
                    Expr: Term
                          | Expr '+' Term {quantity: add Expr.quantity Term.quantity}
                          | Expr '-' Term {quantity: sub Expr.quantity Term.quantity}
                    Term: Factor
                          | Term '*' Factor {quantity: mul Term.quantity Factor.quantity}
                          | Term '/' Factor {quantity: div Term.quantity Factor.quantity}
                    Factor: Number {quantity: dimensionless Number.value}
                              | Number UnitExpr {quantity: quantity Number.value UnitExpr.unit}
                              | '(' Expr ')'
                    Number: Decimal
                                | Decimal 'e' Integer {value: e Decimal.value Integer.value}
                                | Decimal 'p' Integer {value: p Decimal.value Integer.value}
                    Decimal: '-'? [0-9]+ ('.' [0-9]+)? {value: decimal}
                    Integer:  '-'? [0-9]+ {value: decimal}
                    UnitExpr: Unit
                                 | Prefix Unit {unit: prefixUnit Prefix Unit.unit }
                                 | Unit '/' Unit {unit: unitDiv Unit[0].unit Unit[1].unit}
                               #| Unit Unit {unit: unitMul Unit[0].unit Unit[1].unit}
                    Unit: ("m"|"L"|"s"|"h"|"min"|"days"|"weeks"|"years"|"Hz"|"b"|"B") {unit: unit}
                    Prefix: ("n"|"μ"|"m"|"K"|"M"|"G"|"Ki"|"Mi"|"Gi")
                                                      )"_);
        const ref<byte>& input =  "100Kib/s"_;
                //const ref<byte>& input =  "1GiB/100Kib/s"_;
        Node result = parser.parse(input);
        log(input,"=",result.values.at("quantity"_));
    }
} test; 
