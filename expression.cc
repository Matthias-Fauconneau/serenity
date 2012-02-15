#include "expression.h"
#include <math.h>

bool operator==(const Expression& a, const Expression& b) {
    if(a.type != b.type) return false;
    if(a.type == Integer) return a.integer==b.integer;
    if(a.type == Symbol) return a.symbol==b.symbol;
    if(a.type == Sub || a.type == Div) return *a.a == *b.a;
    if(a.type == Add || a.type == Mul || a.type == Pow) return *a.a == *b.a && *a.b == *b.b;
    fail();
}

bool operator>(const Expression& a, int integer) {
    if(a.type == Integer) return a.integer > integer; //integer
    if(a.type == Symbol) return 1>integer; //dummy
    else if(a.type == Sub) return !(*a.a > -integer);
    else if(a.type == Div && a.a->type == Integer) return 1 > integer * a.a->integer; //reciprocal
    else if(a.type == Mul && a.a->type == Integer && a.b->type == Div && a.b->a->type == Integer) //ratio
        return a.a->integer > integer * a.b->a->integer;
    else error(a);
}

Expression abs(const Expression& a) {
    if(a.type == Integer) return abs(a.integer); //integer
    else if(a.type == Symbol) return copy(a); //dummy
    else if(a.type == Div && a.a->type == Integer) return 1/Expression(abs(a.a->integer)); //reciprocal
    else if(a.type == Mul && a.a->type == Integer) return abs(a.a->integer) * *copy(a.b); //ratio
    else error(a);
}

void Expression::invariant() const {
    assert(this);
    assert(type!=Invalid);
    if(type!=Integer) assert(!integer);
    if(type==Symbol) assert(symbol); else assert(!symbol);
    if(type==Symbol||type==Integer) assert(!a),assert(!b);
    if(type==Sub||type==Div) a->invariant(), assert(!b);
    if(type==Add||type==Mul||type==Pow) a->invariant(), b->invariant();
    if(type==Div && a->type==Integer) assert(a->integer!=0,*this);
}

bool Expression::contains(const Expression& e) const {
    if(*this==e) return true;
    if(type == Integer || type == Symbol) return false;
    if(type == Sub||type == Div) return a->contains(e);
    if(type == Add || type == Mul || type == Pow) return a->contains(e) || b->contains(e);
    fail();
}

// helper method to add bracket outer has operator precedence over inner
string bracket(int outer, const Expression* a) {
    int inner = a->type;
    if(outer==Add && inner==Sub) inner=0;
    if(outer==Mul && inner==Div) inner=0;
    //return string("("_+a->str()+")"_);
    return inner > Unary && outer > inner ? string("("_+a->str()+")"_) : a->str();
}

string Expression::str() const {
    if(type==Integer) return ::str(integer);
    if(type==Symbol) return copy(*symbol);

    if(type==Sub) return "-"_+a->str();
    if(type==Div) {
        if(a->type == Integer && a->integer<0) return "-1/"_+::str(-a->integer);
        return "1/"_+a->str();
    }

    string A = bracket(type, a);
    string B = bracket(type, b);
    if(type==Add) return replace(A+"+"_+B,"+-"_,"-"_);
    if(type==Mul)  return replace(A+"*"_+B,"*1/"_,"/"_);
    if(type==Pow) {
        if(b->type == Integer && b->integer==2) return A+"²"_;
        if(b->type == Integer && b->integer==3) return A+"³"_;
        return A+"^"_+B;
    }
    fail();
}

float Expression::toFloat() {
    if(type == Integer) return integer;
    if(type == Symbol) return 1; //dummy
    if(type == Sub) return -a->toFloat();
    if(type == Div) return 1/a->toFloat();
    if(type == Add) return a->toFloat()+b->toFloat();
    if(type == Mul) return a->toFloat()*b->toFloat();
    if(type == Pow) return pow(a->toFloat(),b->toFloat());
    fail();
}

/// Compare two floats allowing for an error of one unit in the last place
inline int ulp(float a, float b) {
    int i = *(int*)&a; if (i<0) i=0x80000000-i;
    int j = *(int*)&b; if (j<0) j=0x80000000-j;
    return abs(i-j);
}

/// Returns the largest positive integer that divides the numbers without a remainder
inline int gcd(int a, int b) { while(b != 0) { int t = b; b = a % b; a = t; } return a; }

void Expression::reduce() { //TODO: generated parser
    debug(Expression before=copy(*this); float check = toFloat();)
    debug(invariant();)
    if(type==Integer||type==Symbol) {}
    else if(type==Sub) {
        if(a->type==Integer) { *this= - a->integer; } //opposite integer
        else if(a->type==Sub) *this= move(*a->a);
        else if(a->type==Mul && a->a->type==Integer) { a->a->integer *= -1; *this=move(*a); } //-(a*b) -> (-a)*b
        else if(a->type==Div && a->a->type==Integer) { a->a->integer *= -1; *this=move(*a); } //-(1/a) -> 1/(-a)
    } else if(type==Div) {
        if(a->type==Mul && a->b->type==Div) *this=move(a->b->a)*(1/move(a->a)); //1/(d/n) -> n/d
        else if(a->type==Div) *this= move(*a->a);  //1/1/a = a
        else if(a->type==Integer && (a->integer==1 || a->integer==-1)) *this= a->integer; //1/1 = 1
    }else if(type==Add) {
        if(a->type==Sub) swap(a,b); //opposite last
        if(a->type==Integer) swap(a,b); //integer last
        if(b->type==Add && a->type!=Add) swap(a,b); //left associative (a+b)+c
        if(a->type==Add && b->type==Add) { *this=*a->a+*a->b+*b->a+*b->b;  //left associative (a+b)+(c+d) = ((a+b)+c)+d
        } else if(b->type ==Integer && b->integer==0) { *this= move(*a); //identity
        } else if(a->type==Integer && b->type ==Integer) { *this= a->integer+b->integer; //add integers
        } else if(b->type==Sub && *b->a==*a) { *this=0; //simplify
        } else if(b->type==Div) { //factorize division: a+1/d -> (a*d+1)/d
            pointer<Expression> d = copy(b->a);
            *this= (move(a) * move(d) + 1) * move(b);
        } else if(b->type==Mul && b->b->type==Div) { //factorize division: a+n/d -> (a*d+n)/d
            pointer<Expression> d = copy(b->b->a);
            *this= (move(a) * move(d) + move(b->a)) * move(b->b);
        } else if(a->type == Add) { //associative sum simplification Σ - Σ
            array<Expression*> ops, terms;
            Expression* e=this; for(;e->type==Add;e=e->a) { ops<<e; terms<<e->b; assert(e->b->type!=Add,*e); } terms<<e;
            bool needReduce=false;
            for(Expression* a: terms) {
                for(Expression* b: terms) { if(a==b) continue;
                    assert(a->type!=Add && b->type!=Add);
                    Expression d = *a +*b;
                    if(d.type == Integer) *a=d.integer;
                    else if(a->type == Mul && b->type == Mul) { //factorization //a*x+b*x = (a+b)*x
                        if(*a->a == *b->a) *a=*a->a*(*a->b+*b->b);
                        else if(*a->a == *b->b) *a=*a->a*(*a->b+*b->a);
                        else if(*a->b == *b->a) *a=*a->b*(*a->a+*b->b);
                        else if(*a->b == *b->b) *a=*a->b*(*a->a+*b->a);
                        else continue;
                    } else continue;
                    *b=0; needReduce=true;
                }
            }
            if(needReduce) {
                for(Expression* op: reverse(ops)) op->reduce();
                reduce();
            }
        }
    } else if(type==Mul) {
        if(b->type==Integer) swap(a,b); //integer first
        if(a->type==Div) swap(a,b); //reciprocal last
        if(b->type==Mul && a->type!=Mul) swap(a,b); //left associative (a*b)*c
        if(a->type==Div && b->type==Div) //factorize division: 1/a*1/b -> 1/(a*b) //TODO->associative
            *this=1/(move(a->a)*move(b->a));
        else if(a->type==Mul && a->b->type==Div && b->type == Div) //factorize division: (a*1/b)*1/c -> a*1/(b*c)  //TODO->associative
            *this=move(a->a)*(1/(move(a->b->a)*move(b->a)));
        else if(a->type==Mul && a->b->type==Div && b->type==Mul && b->b->type==Div) //factorize division: (a*1/b)*(c*1/d) -> (a*c)/(b*d) //TODO->associative
            *this=(move(a->a)*move(b->a))*(1/(move(a->b->a)*move(b->b->a)));
        else if(b->type==Sub) { //factorize (-a)*b -> -(a*b)
            *this=-(move(b->a)*move(a));
        } else if(a->type==Integer) {
           if(b->type==Mul && b->a->type==Integer) a->integer *= b->a->integer, b=move(b->b); //TODO ->associative simplification
           if(a->integer==0) *this=0; //null: a*0 -> 0
           else if(a->integer==1) *this=move(*b); //identity: a*1 -> a
           else if(a->integer==-1) *this=-move(*b); //opposite: a*1 -> a
           else if(b->type==Integer) *this=a->integer*b->integer; //multiply integers  //TODO ->associative simplification
           else if(b->type==Div && b->a->type==Integer) { //simplify integer ratio  //TODO ->associative simplification
               if(b->a->integer<0) a->integer *= -1, b->a->integer *=-1;
               int f=gcd(abs(a->integer),b->a->integer);
               a->integer/=f; b->a->integer/=f;
               if(b->a->integer==1) *this=move(*a); //a/1 -> a
               else if(a->integer==1) *this=move(*b); //1*1/a -> 1/a
               else if(a->integer==-1) *this=-move(*b); //-1*1/a -> -1/a
           }
        } else { //associative product simplification Π / Π
            array<Expression*> ops, num, den;
            Expression* e=this; for(;e->type==Mul;e=e->b) { ops<<e; num<<e->a; }
            if(e->type == Div) {
                ops<<e; e=e->a; for(;e->type==Mul;e=e->b) { ops<<e; den<<e->a; } den << e;
                bool needReduce=false;
                for(Expression* n: num) for(Expression* d: den) {
                    if(*n==*d) { *d=1; *n=1; needReduce=true; }
                }
                if(needReduce) {
                    for(Expression* op: reverse(ops)) op->reduce();
                    reduce();
                }
            }
        }
    } else if(type==Pow) {
        if(b->type == Integer) {
            if(b->integer == 0 && *a) *this=1;
            else if(b->integer == 1) *this=move(*a);
        }
    } else fail();
    debug(invariant();)
    /// Reduce out contract invariants
    if(type==Mul) if(a->type==Integer) assert(a->integer!=1,before,*this);
    if(type==Sub) assert(a->type!=Sub);
    if(type==Div) assert(a->type!=Div);
    debug( if(ulp(check,toFloat())>11) { log(::str(check), before); log(::str(toFloat()),*this); log("ULP distance"_,ulp(check,toFloat())); fail(); })
}

