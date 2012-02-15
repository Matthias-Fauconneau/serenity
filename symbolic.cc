#include "process.h"
#include "expression.h"
#include "algebra.h"

/// Sum of coefficients multiplied by a variable raised to an integer (e.g cx²+bx+a)
/*TODO:
- Polynomial division
- Polynomial GCD
- Square-free polynomial factorization
*/
struct Polynomial : array<Expression> /*coefficients*/ {
    Expression x;

    ///  constructs an empty polynomial
    explicit Polynomial(const Expression& x) : x(copy(x)) { assert(x.type==Symbol); }
    ///  constructs an empty polynomial with space for \a size coefficients
    explicit Polynomial(const Expression& x, int size) : Polynomial(x) { fill(0,size); }
    /// constructs a polynomial from an array of coefficients
    Polynomial(const Expression& x, array&& coefficients) : array<Expression>(move(coefficients)), x(copy(x)) {}
    /// Parses a polynomial in x from expression
    Polynomial(const Expression& x, const Expression& expression) : x(copy(x)) {
        array<const Expression*> terms;
        for(const Expression* e=&expression;;) {
            assert(!(e->a->type == Add && e->b->type == Add));
            if(e->a->type == Add) { terms << e->b; e=e->a;  }
            else if(e->b->type == Add) { terms<<e->a; e=e->b; }
            else { terms << e->a << e-> b; break; }
        }
        for(const Expression* term: terms) {
            const Expression* coef=term; int degree=0;
            if(coef->type == Sub) coef=coef->a;
            if(coef->type == Mul) {
                bool flag[2]={0,0};
                const Expression* ops[] = {coef->a,coef->b};
                for(int i=0;i<2;i++) { const Expression& e=*ops[i];
                    if(e == x) { degree++; flag[i]=true; }
                    else if(e.type == Pow && *e.a==x) { assert(e.b->type==Integer); degree+=e.b->integer; flag[i]=true; }
                }
                if(flag[0] && flag[1]) coef=0; else if(flag[0]) coef=ops[1]; else if(flag[1]) coef=ops[0]; //else degree=0
            } else if(coef->type == Pow && *coef->a==x) { assert(coef->b->type==Integer); degree=coef->b->integer; coef=0;
            } else if(*coef == x) { degree=1; coef=0; }
            if(coef) assert(!coef->contains(x),x,expression,*term); //degree=0
            while(size <= degree) append(0);
            at(degree) += coef? (term->type==Sub?-1:1)*copy(*coef) : 1;
        }
    }
    Polynomial derivative() {
        Polynomial d(x); d.resize(size-1);
        for(int i=1;i<size;i++) {
            d[i-1] = i*at(i);
        }
        return d;
    }
    operator Expression() const {
        Expression e=0;
        //for(int i=0;i<size;i++) e += at(i)*(x^i);
        for(int i=size-1;i>=0;i--) e += at(i)*(x^i);
        return e;
    }
};
template<> Polynomial copy(const Polynomial& p) { return Polynomial(p.x,copy<Expression>(p)); }
template<> string str(const Polynomial& p) { return str((Expression)p); }

/// Multiply polynoms
Polynomial operator*(const Polynomial& A,const Polynomial& B) {
    assert(A.x==B.x);
    Polynomial P(A.x,A.size+B.size);
    for(int i=0;i<A.size;i++) for(int j=0;j<B.size;j++) {
        P[i+j] += A[i]*B[j];
    }
    return P;
}

/// convenience macro to emulate multiple return arguments
#define multi(A, B, F) auto A##B_ F auto A = move(A##B_.A); auto B=move(A##B_.B);

/// Performs polynomial long division using synthetic division algorithm
struct QR { Polynomial Q,R; };
QR division(const Polynomial& P,const Polynomial& D) {
    assert(P.x == D.x && P.size>D.size);
    array<Expression> T = copy(P); //result line R:Q
    for(int j=P.size-1;j>=0;j--) { //for j > D
        for(int i=max(1,(D.size-1)-j);i<min(P.size-j,D.size);i++) { // diagonal multiply T * high(D) / D(0), substract from T
            T[j] -= T[j+i] * D[D.size-1-i] / D[D.size-1];
        }
    }
    for(int j=P.size-1;j>=D.size-1;j--) T[j] /= D[D.size-1];
    Polynomial R(P.x); R << T.slice(0,D.size-1);
    Polynomial Q(P.x); Q << T.slice(D.size-1);
    return i({ move(Q), move(R) });
}

/// Returns quotient of polynomial long division
Polynomial operator/(const Polynomial& P,const Polynomial& D) { return division(P,D).Q; }

/// Returns remainder of polynomial long division
Polynomial operator%(const Polynomial& P,const Polynomial& D) { return division(P,D).R; }

Expression integrate(const Expression& x,const Expression& integrand) {
    assert(x.type==Symbol);
    log(integrand);
    //TODO: rational function integration
    // (x+b)^-1 -> ln|x+b|
    // (x+b)^n -> (x+b)^(n+1) / (n+1)
    return Expression();
}

struct Test : Application { /// Integrals
    Expression x="x"__, x2=x^2, x3=x^3, x4=x^4;
    void test(const Expression& p, const Expression& d) {
        Polynomial P(x,p), D(x,d);
        multi(Q,R, = division(P,D);)
        log("P:"_,P, "\tD:"_,D, "  \tQ:"_,Q, "     \tR:"_,R);
        assert(Polynomial(x,Q*D+R) == P, "Q*D+R"_, Polynomial(x,Q*D+R), P);
    }
    Test() {
        test(x3-12*x2-42,                                    x-3);
        test(x3-12*x2-42,                                    x2+x-3);
        test(6*x3+5*x2-7,                                   3*x2-2*x-1);
        test(4*x4+3*x3+2*x+1,                          x2+x+2);
        test((x^6)+2*x4+6*x-9,                          x3+3);
        test(2*(x^5)-5*x4+7*x3+4*x2-10*x+11, x3+2);
        test(10*(x^5)+x3+5*x2-2*x-2,                5*x2-2);

        /*const Expression &a = "a"__, &b="b"__, &c="c"__,
        Polynomial p(x,c*x2+b*x+a);
        log(p,p.derivative()); //cx²+bx+a*/
        /*integrate(a,1/(a*x+b)); //da/(x+b)
        integrate(a,1/((a+b)^2)); //da/(x+b)²
        integrate(a,1/((a^2)+b)); //da/(x²+b)*/
    }
} test;
