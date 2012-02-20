#include "expression.h"
#include <math.h>

bool operator==(const Expression& a, const Expression& b) {
    if(a.type != b.type) return false;
    if(a.type == Integer) return a.integer==b.integer;
    if(a.type == Symbol) return a.symbol==b.symbol;
    if(a.type == Add || a.type == Mul || a.type == Pow) return *a.a == *b.a && *a.b == *b.b;
    fail();
}

bool operator>(const Expression& a, int integer) {
    if(a.type == Integer) return a.integer > integer; //integer
    if(a.type == Symbol) return 1>integer; //dummy
    error(a);
}

Expression abs(const Expression& a) {
    if(a.type == Integer) return abs(a.integer); //integer
    if(a.type == Symbol) return copy(a); //dummy
    if(a.type == Mul && a.a->type == Integer) return abs(a.a->integer) * *copy(a.b); //ratio
    error(a);
}

void Expression::invariant() const {
    assert(this);
    assert(type>Invalid && type <= Pow);
    if(type!=Integer) assert(!integer);
    if(type==Symbol) assert(symbol); else assert(!symbol);
    if(type==Symbol||type==Integer) assert(!a),assert(!b);
    if(type==Add||type==Mul||type==Pow) a->invariant(), b->invariant();
    if(type==Pow && b->type==Integer && b->integer<0) assert(*a);
}

bool Expression::contains(const Expression& e) const {
    if(*this==e) return true;
    if(type == Integer || type == Symbol) return false;
    if(type == Add || type == Mul || type == Pow) return a->contains(e) || b->contains(e);
    fail();
}

const Expression* Expression::find(bool (*match)(const Expression&)) const {
    if(match(*this)) return this;
    if(type == Integer || type == Symbol) return 0;
    if(type == Add || type == Mul || type == Pow) return a->find(match)?:b->find(match)?:0;
    fail();
}

int Expression::count() {
    if(type == Integer || type == Symbol) return 1;
    if(type == Add || type == Mul || type == Pow) return a->count()+b->count();
    fail();
}

string Expression::str() const {
    invariant();
    if(type==Integer) return ::str(integer);
    if(type==Symbol) return copy(*symbol);

    string A = a->type<Operator || a->type>=type ? a->str() : "("_+a->str()+")"_;
    string B = b->type<Operator || b->type>=type ? b->str() : "("_+b->str()+")"_;
    if(type==Add) return replace(A+"+"_+B,"+-"_,"-"_);
    if(type==Mul)  {
        if(a->type==Pow && a->b->type==Integer && a->b->integer==-1) return B+slice(A,1);
        if(b->type==Integer && a->type!=Integer) return B+A;
        return A+"*"_+B;
    }
    if(type==Pow) {
        if(b->type == Integer && b->integer==-1) return "1/"_+A;
        const string superscript[] = {"⁰"_,"¹"_,"²"_,"³"_,"⁴"_,"⁵"_,"⁶"_,"⁷"_,"⁸"_,"⁹"_,"¹⁰"_,"¹¹"_};
        if(b->type == Integer && b->integer>1 && b->integer<=11) return A+superscript[b->integer];
        return A+"^"_+B;
    }
    fail();
}

float Expression::toFloat() {
    if(type == Integer) return integer;
    if(type == Symbol) return 1; //dummy
    if(type == Add) return a->toFloat()+b->toFloat();
    if(type == Mul) return a->toFloat()*b->toFloat();
    if(type == Pow) {
        assert(a->toFloat(),*this);
        return powf(a->toFloat(),b->toFloat());
    }
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
    debug(
            invariant();
            Expression before=copy(*this);
            float check = toFloat();
            //int size=count();
    )
    if(type==Integer||type==Symbol) {}
    else if(type==Add) {
        if(a->type==Integer) swap(a,b); //integer last
        if(b->type==Add && a->type!=Add) swap(a,b); //left associative (a+b)+c
        if(a->type==Add && b->type==Add) *this= new(new(move(a->a) + move(a->b)) + move(b->a)) + move(b->b);  //left associative (a+b)+(c+d) = ((a+b)+c)+d
        else if(b->type==Integer && b->integer==0) *this= move(*a); //identity
        else if(a->type==Integer && b->type ==Integer) *this= a->integer+b->integer; //add integers
        else if(a->type==Mul && a->b->type==Integer && a->b->integer == -1 && *a->a == *b) *this=0; //simplify
        else if(a->type==Mul && a->b->type==Integer && a->b->integer == -1 && a->a->type==Integer && b->type==Integer) //substract integers
            *this= b->integer-a->a->integer;
        else if(a->type == Add) { //associative sum simplification Σ - Σ (factorize code with product)
            array<Expression*> ops, terms;
            Expression* e=this; for(;e->type==Add;e=e->a) { ops<<e; terms<<e->b; assert(e->b->type!=Add,*e); } terms<<e;
            bool needReduce=false;
            for(Expression* a: terms) {
                for(Expression* b: terms) { if(a==b) continue;
                    assert(a->type!=Add && b->type!=Add);
                    Expression d = *a +*b;
                    if(d.type == Integer) *a=d.integer;
                    /*else if(a->type == Mul && b->type == Mul) { //factorization //a*x+b*x = (a+b)*x
                        if(*a->a == *b->a) *a=*a->a*(*a->b+*b->b);
                        else if(*a->a == *b->b) *a=*a->a*(*a->b+*b->a);
                        else if(*a->b == *b->a) *a=*a->b*(*a->a+*b->b);
                        else if(*a->b == *b->b) *a=*a->b*(*a->a+*b->a);
                        else continue;
                    }*/ else continue;
                    *b=0; needReduce=true;
                }
            }
            if(needReduce) {
                for(Expression* op: reverse(ops)) op->reduce();
                reduce();
            }
        } else if(a->type==Pow && a->b->type==Integer && a->b->integer==-1) { //factorize division: 1/d+n = (n*d+1)/d
            auto d=copy(a->a);
            *this = new(new(move(b) * move(d)) + new(1)) * move(a);
        } else if(a->type == Mul && a->a->type==Pow && a->a->b->type==Integer && a->a->b->integer==-1) { //factorize division: (1/d*a)+n = (n*d+a)/d
            auto d=copy(a->a->a);
            *this = new(new(move(b) * move(d)) + move(a->b)) * move(a->a);
        }
    } else if(type==Mul) {
        if(a->type==Integer) swap(a,b); //integer last
        if(b->type==Mul && a->type!=Mul) swap(a,b); //left associative (a*b)*c
        if(a->type==Mul && b->type==Mul) *this= new(new(move(a->a) * move(a->b)) * move(b->a)) * move(b->b);  //left associative (a*b)*(c*d) = ((a*b)*c)*d
        else if(b->type==Integer && b->integer==0) *this=0; //null: a*0 -> 0
        else if(b->type==Integer && b->integer==1) *this= move(*a); //identity: a*1 -> a
        else if(b->type==Integer && a->type==Integer) *this= b->integer*a->integer; //multiply integers  //TODO ->associative simplification
        else if(a->type==Mul) { //associative product simplification Π / Π
            assert(b->type != Mul);
            array<Expression*> ops, terms;
            Expression* e=this; for(;e->type==Mul;e=e->a) { ops<<e; terms<<e->b; assert(e->b->type!=Mul,*e); } terms<<e;
            bool needReduce=false;
            for(Expression* a: terms) {
                for(Expression* b: terms) { if(a==b) continue;
                    assert(a->type!=Mul && b->type!=Mul);
                    Expression d = *a * *b;
                    if(d.type == Integer) *a=d.integer;
                    else if(d.count() < a->count()+b->count()) *a=move(d);
                    else continue;
                    *b=1; needReduce=true;
                }
            }
            if(needReduce) {
                for(Expression* op: reverse(ops)) op->reduce();
                reduce();
            }
        } else if(a->type==Pow) {
            if(b->type == Pow && *a->b==*b->b) *this= new(move(a->a) * move(b->a))^move(a->b); //factorize a^c*b^c = (a*b)^c
            else if(b->type == Pow && *a->a==*b->a) *this= move(a->a) ^ new(move(a->b)+move(b->b)); //factorize a^b*a^c = a^(b+c)
            else if(*a->a == *b) *this= move(a->a) ^ new(move(a->b)+new(1)); //factorize a^b*a -> a^(b+1)
            else if(a->b->type==Integer && a->b->integer == -1 && a->a->type==Integer && b->type==Integer) { //integer ratio
                int q = gcd(b->integer,a->a->integer);
                b->integer /= q; a->a->integer /= q;
                if(a->a->integer<0) b->integer *= -1, a->a->integer *= -1;
                if(a->a->integer==1) *this= move(*b); //a/1 -> a
                else if(a->a->integer==-1) *this= new(-1)*move(b); //-1*1/a -> -1/a
                else if(b->integer==1) *this= move(*a); //1*1/a -> 1/a
            }
        }
    } else if(type==Pow) {
        if(a->type==Integer && a->integer==1) *this= 1; //1^p = 1
        else if(a->type==Integer && a->integer==0) *this= 0; //0^p = 0
        else if(b->type == Integer && b->integer == 0) *this= 1; //x^0 = 1
        else if(b->type == Integer && b->integer == 1) *this= move(*a); //x^1 = x
        else if(a->type == Mul && a->a->type==Pow) { //factorize (a^b*c)^d = a^(b*d)*c^d (e.g 1/(1/a*c) = c*1/a)
            auto d = copy(b);
            *this= new(move(a->a->a) ^ new(move(a->a->b) * move(d))) * new(move(a->b) ^ move(b));
        }
    } else fail();
    debug(
            invariant();
            /// Reduce out contract invariants
            if(type==Mul && a->type==Integer) assert(a->integer!=1,before,*this);
            //assert((abs(toFloat())<1<<16), before, *this);
            if(ulp(check,toFloat())>134) { log(::str(check), before); log(::str(toFloat()),*this); log("ULP distance"_,ulp(check,toFloat())); fail(); }
            //assert(count()<=size, size,count(),before,*this);
    )
}

