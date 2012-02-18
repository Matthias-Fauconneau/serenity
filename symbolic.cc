#include "process.h"
#include "expression.h"
//#include "algebra.h"

/// Sum of coefficients multiplied by a variable raised to an integer (e.g cx²+bx+a)
/*TODO:
- Polynomial root
- Square-free polynomial factorization
*/
struct Polynomial : array<Expression> /*coefficients*/ {
    const string* x=0;

    ///  constructs an empty polynomial with space for \a size coefficients
    Polynomial(const string* x, int size) : x(x) { fill(0,size); }
    /// constructs a polynomial from an array of coefficients
    Polynomial(const string* x, array&& coefficients) : array<Expression>(move(coefficients)), x(x) {
        while(size && !last()) pop();
    }
    /// Parses a polynomial from \a expression deducing the indeterminate variable
    Polynomial(const Expression& e);

    explicit operator Expression() const {
        Expression e=0;
        for(int i=size-1;i>=0;i--) e += at(i)*(Expression(x)^i);
        return e;
    }
};
template<> Polynomial copy(const Polynomial& p) { return Polynomial(p.x,copy<Expression>(p)); }
static const string superscript[] = {"⁰"_,"¹"_,"²"_,"³"_,"⁴"_,"⁵"_,"⁶"_,"⁷"_,"⁸"_,"⁹"_,"¹⁰"_,"¹¹"_};
template<> string str(const Polynomial& p) {
    string s;
    assert(p.size-1<=11);
    for(int i=p.size-1;i>=0;i--) { s << str(p[i]); if(i>0) s<<*p.x; if(i>1) s << superscript[i]; if(i>0) s<<" + "_; }
    return s?move(s):"0"_;
}

/// Compare polynoms
bool operator==(const Polynomial& A,const Polynomial& B) {
    assert(!A.x || !B.x || A.x==B.x);
    return operator==<Expression>(A,B);
}
bool operator!=(const Polynomial& A,const Polynomial& B) { return !(A==B); }

/// Add polynoms
Polynomial operator+(const Polynomial& A,const Polynomial& B) {
    assert(!A.x || !B.x || A.x==B.x);
    Polynomial P(A.x?:B.x,max(A.size,B.size));
    for(int i=0;i<max(A.size,B.size);i++) {
        if(i<A.size) P[i] += A[i];
        if(i<B.size) P[i] += B[i];
    }
    while(P.size && !P.last()) P.pop();
    return P;
}

/// Substract polynoms
Polynomial operator-(const Polynomial& A,const Polynomial& B) {
    assert(!A.x || !B.x || A.x==B.x);
    Polynomial P(A.x?:B.x,max(A.size,B.size));
    for(int i=0;i<max(A.size,B.size);i++) {
        if(i<A.size) P[i] += A[i];
        if(i<B.size) P[i] -= B[i];
    }
    while(P.size && !P.last()) P.pop();
    return P;
}

/// Multiply polynoms
Polynomial operator*(const Polynomial& A,const Polynomial& B) {
    assert(!A.x || !B.x || A.x==B.x);
    Polynomial P(A.x?:B.x,A.size-1+B.size-1+1);
    for(int i=0;i<A.size;i++) for(int j=0;j<B.size;j++) {
        P[i+j] += A[i]*B[j];
    }
    return P;
}

Polynomial::Polynomial(const Expression& e) {
    if(e.type==Integer) *this=Polynomial(0,{copy(e)});
    else if(e.type==Symbol) *this=Polynomial(e.symbol,{0,1});
    else if(e.type == Pow && e.a->type==Symbol) { assert(e.b->type==Integer); x=e.a->symbol; fill(0,e.b->integer+1); at(e.b->integer)=1; }
    else if(e.type==Add) *this=Polynomial(*e.a)+Polynomial(*e.b);
    else if(e.type==Mul) *this=Polynomial(*e.a)*Polynomial(*e.b);
    else error((int)e.type);
}

/// convenience macro to emulate multiple return arguments
#define multi(A, B, F) auto A##B_ F auto A = move(A##B_.A); auto B=move(A##B_.B);

/// Performs polynomial long division using synthetic division algorithm
struct QR { Polynomial Q,R; };
QR division(const Polynomial& P,const Polynomial& D) {
    assert(D.size && P.x == D.x);
    if(P.size<D.size) return i({ copy(P), copy(D) });
    assert(P.last()); assert(D.last());
    array<Expression> T = copy(P);
    for(int j=P.size-1;j>=0;j--) {
        for(int i=max(1,(D.size-1)-j);i<min(P.size-j,D.size);i++) {
            T[j] -= T[j+i] * D[D.size-1-i] / D.last();
        }
    }
    for(int j=P.size-1;j>=D.size-1;j--) T[j] /= D.last();
    return i({ Polynomial(P.x, T.slice(D.size-1)), Polynomial(P.x,T.slice(0,D.size-1)) });
}

/// Returns quotient of polynomial long division
Polynomial operator/(const Polynomial& P,const Polynomial& D) { return division(P,D).Q; }

/// Returns remainder of polynomial long division
Polynomial operator%(const Polynomial& P,const Polynomial& D) { return division(P,D).R; }

/// Divides each coefficient by the leading coefficient to make the polynomial monic
Polynomial monic(Polynomial&& P) {
    for(int i=0;i<P.size;i++) P[i] /= P.last();
    return move(P);
}

/// Returns the largest monic polynomial that divides the polynomial
Polynomial gcd(const Polynomial& A,const Polynomial& B) {
    Polynomial a=monic(copy(A)), b=monic(copy(B));
    while(b) {
        Polynomial t = copy(b);
        b = monic(a%b);
        //if(b) log("A"_,a,"B"_,t,"R"_,b);
        a = move(t);
    }
    return a;
}

/// Returns the polynomial derivative
Polynomial derivative(const Polynomial& P) {
    Polynomial d(P.x,P.size-1);
    for(int i=1;i<P.size;i++) {
        d[i-1] = i*P[i];
    }
    return d;
}

/// Returns polynom to the nth power
Polynomial pow(const Polynomial& A,int n) {
    Polynomial P(A.x,array<Expression>{1});
    for(int i=0;i<n;i++) P = P*A;
    assert(P.last(),P,P.size);
    return P;
}

/// Returns highest n coefficients (i.e zero lowest coefficients)
Polynomial highest(Polynomial&& A,int n) {
    if(n>=A.size) return Polynomial(A.x,1);
    assert(n<=A.size && A.last(),n, A.size, A);
    if(n==A.size) return Polynomial(A.x,1);
    for(int i=0;i<n;i++) A[i]=0;
    return move(A);
}

/// polynom Nth root
Polynomial root(const Polynomial& P,int n) {
    const auto& x=Expression(P.x);
    int i = (P.size-1)/n;
    assert(n*i == (P.size-1) && P.last()==1, P.size-1, i, n, P);
    //error(A,p);
    Polynomial Q = x^i;
    log("P"_,P, "\tQ"_,Q);
    for(int j=n;j>=1;j--) {
        log(i+j, "Q"_,Q, "\tQ²"_, pow(Q,n), "\tP-Q²"_, P-pow(Q,n), "\tP-Q²_"_, highest(P-pow(Q,n),i+j-1));
        Q = Q + highest(P-pow(Q,n),i+j-1) / Polynomial(n*(x^i));
        //log("Q"_,Q);
    }
    assert(P.last());
    return move(Q);
#if 0
    const auto& x=P.x;
    int i = (P.size-1)/n;
    assert(n*i == (P.size-1) && P.last()==1, P.size-1, i, n, P);
    //error(A,p);
    Polynomial Q = x^i;
    log("P"_,P, "\tQ"_,Q);
    for(int j=n;j>=1;j--) {
        log(i+j, "Q"_,Q, "\tQ²"_, pow(Q,2), "\tP-Q²"_, P-pow(Q,2), "\tP-Q²_"_, highest(P-pow(Q,n),i+j-1));
        Q = Q + highest(P-pow(Q,2),i+j-1) / Polynomial(n*(x^i));
        //log("Q"_,Q);
    }
    assert(P.last());
    return move(Q);
    //if(n==2) {
    /*} else if(n==3) {
        int u = (P.size-1)/3,  r=(P.size-1)%3;
        Polynomial Q(x,u+(1-(3-r)/2));
        log(u,u+(1-(3-r)/2),u+r/2,u);
        for(int i=0;i<u+(1-(3-r)/2);i++) Q[i]+=P[3*i];
        for(int i=0;i<u+r/2;i++) Q[i/3]+=P[3*i+1];
        for(int i=0;i<u;i++) Q[i/3]+=P[3*i+2];
        return move(Q);
    } else fail();*/
        int i = (P.size-1)/n;
        assert(n*i == (P.size-1) && P.last()==1, P.size-1, i, n, P);
        //error(A,p);
        Polynomial Q = x^i;
        log("P"_,P, "\tQ"_,Q);
        for(int j=n;j>=1;j--) {
            log(i+j, "Q"_,Q, "\tQn"_, pow(Q,n), "\tQn-P"_, pow(Q,n)-P, "\tQn-P/(Qn-P)'_"_, ((pow(Q,n)-P) / derivative(pow(Q,n)-P)));
            Q = Q - ((pow(Q,n)-P) / derivative(pow(Q,n)-P));
                    //,i+j-1) / Polynomial(n*(x^i));
            //log("Q"_,Q);
        }
        assert(P.last());
        return /*monic(*/move(Q);//);
#endif
}

Expression factorize(const Polynomial& P) {
    assert(P.last()==1, P);
    int i=1;
    Expression output=1;
    Polynomial f = copy(P);
    Polynomial g = derivative(f);
    log("g"_,g);
    if(g) {
        Polynomial c = gcd(f,g);
        Polynomial w = f/c; //V
        log("c"_,c,"\tw"_,w);
        while(w!=Polynomial(1)) {
            Polynomial y = gcd(w, c)/*V+1*/, z = w/y;
            log(i,w,c,"y"_,y,"\tz"_,z,"\toutput"_,output);
            output *= (Expression)(z)/*(z^i)*/; i++;
            c = c/y; w=move(y);
            log(i,"c"_,c,"\tw"_,w,"\toutput"_,output);
        }
        log("·"_,c,f,w);
        if(c!=Polynomial(1)) {
            log(i,"root"_,c);
            c=root(c,i);
            output *= (Expression)pow(factorize(c),i);
        }
    } else {
        log(i,"root"_,f);
        f=root(f,i);
        output *= (Expression)pow(factorize(f),i);
    }
    return output;
}

/*Expression integrate(const Expression& x,const Expression& integrand) {
    assert(x.type==Symbol);
    log(integrand);
    //TODO: rational function integration
    // (x+b)^-1 -> ln|x+b|
    // (x+b)^n -> (x+b)^(n+1) / (n+1)
    return Expression();
}*/

struct Test : Application { /// Integrals
    Expression x="x"__, x2=x^2, x3=x^3, x4=x^4;

    void divisionTest(const Polynomial& P, const Polynomial& D) {
        multi(Q,R, = division(P,D);)
        log("P:"_,P, "\tD:"_,D, "  \tQ:"_,Q, "     \tR:"_,R);
        assert(Q*D+R == P, "Q*D+R"_, Q*D+R, P);
    }
    void gcdTest(const Polynomial& A, const Polynomial& B) {
        auto Q = gcd(A,B);
        log("A:"_,A, "\tB:"_,B, "\tQ:"_,Q, "\tA/Q:"_,A/Q, "\tB/Q:"_,B/Q);
        assert((A/Q)*(B/Q) == (A*B)/(Q*Q), (A/Q)*(B/Q) , A*B, (A*B)/(Q*Q) );
    }
    void rootTest(const Polynomial& A, int n) {
        auto P = pow(A,n);
        auto R = root(P,n);
        log("A"_,A,"\tA"_+superscript[n],P,"\tR"_,R);
        assert(R==A);
    }
    void factorizeTest(const Polynomial& A, int n) {
        auto P = pow(A,n);
        auto R = factorize(P);
        log("A"_,A,"\tA"_+superscript[n],P,"\tR"_,Polynomial(R));
        assert(R==P);
    }

    Test() {
        /*divisionTest(x3-12*x2-42,                                    x-3);
        divisionTest(x3-12*x2-42,                                    x2+x-3);
        divisionTest(6*x3+5*x2-7,                                   3*x2-2*x-1);
        divisionTest(4*x4+3*x3+2*x+1,                          x2+x+2);
        divisionTest((x^6)+2*x4+6*x-9,                          x3+3);
        divisionTest(2*(x^5)-5*x4+7*x3+4*x2-10*x+11, x3+2);
        divisionTest(10*(x^5)+x3+5*x2-2*x-2,                5*x2-2);*/

        gcdTest(x+2,pow(x+2,2));

        //divisionTest(x2+7*x+6, x2-5*x-6);
        //gcdTest(x2+7*x+6, x2-5*x-6);

        //a(x) = x4 − 4x3 + 4 x2 − 3x + 14 = (x2 − 5x + 7)(x2 + x + 2)
        //b(x) = x4 + 8x3 + 12x2 + 17x + 6 = (x2 + 7x + 3)(x2 + x + 2).
        //divisionTest(x4 - 4*x3 + 4*x2 - 3*x + 14, x4 + 8*x3 + 12*x2 + 17*x + 6);
        //gcdTest(x4 - 4*x3 + 4*x2 - 3*x + 14, x4 + 8*x3 + 12*x2 + 17*x + 6);

        //log(Polynomial(x+1)*pow(x2+1,3)*pow(x+2,4));
        //x^{11}+9x^10+35x^9+83x^8+147x^7+211x^6+241x^5+225*x^4+176x^3+104x^2+48x+16
        //Polynomial P = (x^11) + 2*(x^9) + 2*(x^8) + (x^6) + (x^5) + 2*(x^3) + 2*(x^2) + 1;
        //Polynomial P(x,reverse(array<Expression>({1,9,35,83,147,211,241,225,176,104,48,16})));
        //Polynomial P = Polynomial(x+1)*pow(x2+1,3)*pow(x+2,4);
        //log("P "_,P);
        //log("P'"_,derivative(P));
        //divisionTest(P, (x^9) + 2*(x^6) + (x^3) + 2);
        //divisionTest(derivative(P), (x^9) + 2*(x^6) + (x^3) + 2);
        //gcdTest(P,derivative(P));

        //rootTest(x+2,3);
        factorizeTest(x+2,4);
        /*rootTest(x2+2*x+1, 2);
        rootTest(x2+2*x, 2);
        rootTest(x2+1, 2);
        rootTest(x+1, 3);*/
        //rootTest(x2+1, 3);
        //rootTest(x+1, 4);
        //log(root(pow(x2+1,3),3));

        /*const Expression &a = "a"__, &b="b"__, &c="c"__,
        Polynomial p(x,c*x2+b*x+a);
        log(p,p.derivative()); //cx²+bx+a*/
        /*integrate(a,1/(a*x+b)); //da/(x+b)
        integrate(a,1/((a+b)^2)); //da/(x+b)²
        integrate(a,1/((a^2)+b)); //da/(x²+b)*/
    }
} test;
