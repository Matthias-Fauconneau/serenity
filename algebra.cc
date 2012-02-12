#include "process.h"
//TODO: Expression -> expression.cc

const string& operator "" __(const char* data, size_t size) {
    static array<string*> uniques;
    string unique(data,size);
    int i = uniques.find([&unique](string* s){return *s==unique;});
    if(i<0) { i=uniques.size; uniques<<new string(move(unique)); }
    return *uniques[i];
}

/// pointer to a pool-allocated value with move semantics
template<class T> struct pointer {
    no_copy(pointer)
    pointer(){}
    static const int max=8192; //Maximum expression complexity
    static T pool[max];
    static int count;
    pointer(T&& value):value(new (pool+count++) T(move(value))){assert(count<max);} //allocate
    pointer(pointer&& o) : value(o.value) { o.value=0; }
    pointer& operator=(pointer&& o) { this->~pointer(); value=o.value; o.value=0; return *this; }
    ~pointer() { if(value) value->~Expression(); } //TODO: free
    T* value=0;
    const T& operator *() const { return *value; }
    T& operator *() { return *value; }
    const T* operator ->() const { return value; }
    T* operator ->() { return value; }
    explicit operator bool() const { return value; }
    bool operator !() const { return !value; }
    operator const T*() const { return value; }
    operator T*() { return value; }
};
template <class T> T pointer<T>::pool[pointer<T>::max];
template <class T> int pointer<T>::count=0;
template<class T> pointer<T> copy(const pointer<T>& p) { if(!p.value) return pointer<T>(); return pointer<T>(copy(*p.value)); }

//explicit polymorphism to allow expression reduction (*this=)
enum Type { Invalid, /*Operand*/ Symbol, Integer, /*Unary*/ Sub, Div, /*Binary*/ Add, Mul };
struct Expression {
    move_only(Expression)
    Type type=Invalid;
    int integer=0;
    const string* symbol=0;
    pointer<Expression> a, b;
    Expression(){}
    Expression(int integer) : type(Integer), integer(integer) {}
    Expression(const string& symbol) : type(Symbol), symbol(addressof(symbol)) {} /// \note \a symbol should outlive this \a Expression
    Expression(Type type, pointer<Expression>&& a) : type(type), a(move(a)) { assert(type==Sub||type==Div); reduce(); }
    Expression(Type type, pointer<Expression>&& a, pointer<Expression>&& b) : type(type), a(move(a)), b(move(b)) { assert(type==Add||type==Mul); reduce(); }
    void invariant() const {
        assert(this);
        assert(type!=Invalid);
        if(type!=Integer) assert(!integer);
        if(type==Symbol) assert(symbol); else assert(!symbol);
        if(type==Symbol||type==Integer) assert(!a),assert(!b);
        if(type==Sub||type==Div) a->invariant(), assert(!b);
        if(type==Add||type==Mul) a->invariant(), b->invariant();
        if(type==Div && a->type==Integer) assert(a->integer!=0,*this);
    }
    float toFloat();
    void reduce();
    string str() const {
        if(type==Integer) return toString(integer);
        if(type==Symbol) return copy(*symbol);
        if(type==Add) {
            if(b->type == Sub) return "("_+a->str()+"-"_+b->a->str()+")"_;
            return "("_+a->str()+"+"_+b->str()+")"_;
        }
        if(type==Sub) return "-"_+a->str();
        if(type==Mul) {
            if(b->type == Div) return "("_+a->str()+"/"_+b->a->str()+")"_;
            return "("_+a->str()+"*"_+b->str()+")"_;
        }
        if(type==Div) {
            if(a->type == Integer && a->integer<0) return "-1/"_+toString(-a->integer);
            return "1/"_+a->str();
        }
        fail();
    }
    explicit operator bool() const {
        if(type==Integer) return integer; return true;//not zero
    }
};

//TODO: template<> copy
Expression copy(const Expression& e) { Expression r; r.type=e.type; r.integer=e.integer; r.symbol=e.symbol; r.a=copy(e.a); r.b=copy(e.b); return r; }

/// move semantics
Expression operator-(pointer<Expression>&& a) { return Expression(Sub,move(a)); }
Expression operator/(const int a, pointer<Expression>&& b) { assert(a==1); return Expression(Div,move(b)); }

Expression operator+(pointer<Expression>&& a, pointer<Expression>&& b) { return Expression(Add,move(a),move(b)); }
Expression operator-(pointer<Expression>&& a, pointer<Expression>&& b) { return move(a)+-move(b); }
Expression operator*(pointer<Expression>&& a, pointer<Expression>&& b) { return Expression(Mul,move(a),move(b)); }
Expression operator/(pointer<Expression>&& a, pointer<Expression>&& b) { return move(a)*(1/move(b)); }

/// copy semantics //TODO: forward
#define new(a) pointer<Expression>(copy(a))
Expression operator-(const Expression& a) { return -new(a); }
Expression operator/(const int a, const Expression& b) { assert(a==1); return 1/new(b); }

Expression operator+(const Expression& a, const Expression& b) { return new(a)+new(b); }
Expression operator-(const Expression& a, const Expression& b) { return new(a)+-new(b); }
Expression operator*(const Expression& a, const Expression& b) { return new(a)*new(b); }
Expression operator/(const Expression& a, const Expression& b) { return new(a)*(1/new(b)); }

Expression& operator+=(Expression& a, const Expression& b) { return a=a+b; }
Expression& operator-=(Expression& a, const Expression& b) { return a=a-b; }
Expression& operator*=(Expression& a, const Expression& b) { return a=a*b; }

bool operator==(const Expression& a, const Expression& b) {
    if(a.type != b.type) return false;
    if(a.type == Integer) return a.integer==b.integer;
    if(a.type == Symbol) return a.symbol==b.symbol;
    if(a.type == Sub || a.type == Div) return *a.a == *b.a;
    if(a.type == Add || a.type == Mul) return *a.a == *b.a && *a.b == *b.b;
    fail();
}
bool operator!=(const Expression& a, const Expression& b) { return !(a==b); }
bool operator>(const Expression& a, int integer) {
    if(a.type == Integer) return a.integer > integer; //integer
    if(a.type == Symbol) return 1>integer; //dummy
    else if(a.type == Sub) return !(*a.a > -integer);
    else if(a.type == Div && a.a->type == Integer) return 1 > integer * a.a->integer; //reciprocal
    else if(a.type == Mul && a.a->type == Integer && a.b->type == Div && a.b->a->type == Integer) //ratio
        return a.a->integer > integer * a.b->a->integer;
    else error(a);
}
bool operator>(const Expression& a, const Expression& b) { return (a-b)>0; }

Expression abs(const Expression& a) {
    if(a.type == Integer) return abs(a.integer); //integer
    else if(a.type == Symbol) return copy(a); //dummy
    else if(a.type == Div && a.a->type == Integer) return 1/Expression(abs(a.a->integer)); //reciprocal
    else if(a.type == Mul && a.a->type == Integer) return abs(a.a->integer) * *copy(a.b); //ratio
    else error(a);
}

template<> void log_(const Expression& a) { log_(a.str()); }

/// Compare two floats allowing for an error of one unit in the last place
int ulp(float a, float b) {
    int i = *(int*)&a; if (i<0) i=0x80000000-i;
    int j = *(int*)&b; if (j<0) j=0x80000000-j;
    return abs(i-j);
}

float Expression::toFloat() {
    if(type == Integer) return integer;
    if(type == Symbol) return 1; //dummy
    if(type == Sub) return -a->toFloat();
    if(type == Div) return 1/a->toFloat();
    if(type == Add) return a->toFloat()+b->toFloat();
    if(type == Mul) return a->toFloat()*b->toFloat();
    fail();
}

void Expression::reduce() { //TODO: generated parser
    debug(Expression before=copy(*this); float check = toFloat();)
    invariant();
    if(type==Add) {
        if(b->type==Integer) swap(a,b); //integer first
        if(a->type==Sub) swap(a,b); //opposite last
        if(b->type==Div) { //factorize division: a+1/d -> (a*d+1)/d
            pointer<Expression> d = copy(b->a);
            *this= (move(a) * move(d) + 1) * move(b);
        } else if(b->type==Mul && b->b->type==Div) { //factorize division: a+n/d -> (a*d+n)/d
            pointer<Expression> d = copy(b->b->a);
            *this= (move(a) * move(d) + move(b->a)) * move(b->b);
        } else if(a->type ==Integer) {
            if(b->type==Add && b->a->type==Integer) { a->integer+=b->a->integer; b=move(b->b); } //associativity: a+(b+c) -> (a+b)+c
            if(a->integer==0) { *this= move(*b); } //identity: a+0 -> a
            else if(b->type==Integer) { *this= a->integer+b->integer; } //add integers
        } else if(b->type==Sub) { //TODO: associative ratio simplification Σ - Σ
            if(*a == *b->a) *this=0;
        }
    } else if(type==Sub) {
        if(a->type==Integer) { *this= - a->integer; } //opposite integer
        else if(a->type==Mul && a->a->type==Integer) { a->a->integer *= -1; *this=move(*a); } //-(a*b) -> (-a)*b
        else if(a->type==Div && a->a->type==Integer) { a->a->integer *= -1; *this=move(*a); } //-(1/a) -> 1/(-a)
    } else if(type==Mul) {
        if(b->type==Integer) swap(a,b); //integer first
        if(a->type==Div) swap(a,b); //reciprocal last
        if(a->type==Div && b->type==Div) //factorize division: 1/a*1/b -> 1/(a*b)
            *this=1/(move(a->a)*move(b->a));
        else if(a->type==Mul && a->b->type==Div && b->type == Div) //factorize division: (a*1/b)*1/c -> a*1/(b*c)
            *this=move(a->a)*(1/(move(a->b->a)*move(b->a)));
        else if(a->type==Mul && a->b->type==Div && b->type==Mul && b->b->type==Div) //factorize division: (a*1/b)*(c*1/d) -> (a*c)/(b*d)
            *this=(move(a->a)*move(b->a))*(1/(move(a->b->a)*move(b->b->a)));
        else if(a->type==Sub) { //factorize (-a)*b -> -(a*b)
            *this=-(move(a->a)*move(b));
        } else if(a->type==Integer) {
           if(b->type==Mul && b->a->type==Integer) a->integer *= b->a->integer, b=move(b->b); //associativity: a*(b*c) -> (a*b)*c
           if(a->integer==0) *this=0; //null: a*0 -> 0
           else if(a->integer==1) *this=move(*b); //identity: a*1 -> a
           else if(a->integer==-1) *this=-move(*b); //opposite: a*1 -> a
           else if(b->type==Integer) *this=a->integer*b->integer; //multiply integers
           else if(b->type==Div && b->a->type==Integer) { //simplify integer ratio
               if(b->a->integer<0) a->integer *= -1, b->a->integer *=-1;
               int f=gcd(abs(a->integer),b->a->integer);
               a->integer/=f; b->a->integer/=f;
               if(b->a->integer==1) *this=move(*a); //a/1 -> a
               else if(a->integer==1) *this=move(*b); //1*1/a -> 1/a
               else if(a->integer==-1) *this=-move(*b); //-1*1/a -> -1/a
           }
        } else { //associative ratio simplification Π / Π //(TODO: test combinations)
            array<Expression*> ops, num, den;
            Expression* e=this; for(;e->type==Mul;e=e->b) { ops<<e; num<<e->a; }
            if(e->type == Div) {
                ops<<e; e=e->a; for(;e->type==Mul;e=e->b) { ops<<e; den<<e->a; } den << e;
                bool needReduce=false;
                for(Expression* n: num) for(Expression* d: den) {
                    if(*n==*d) { *d=1; *n=1; needReduce=true; }
                }
                if(needReduce) {
                    ops.reverse();
                    for(Expression* op: ops) op->reduce();
                    reduce();
                }
            }
        }
    } else if(type==Div) {
        if(a->type==Mul && a->b->type==Div) *this=move(a->b->a)*(1/move(a->a)); //1/(d/n) -> n/d
        else if(a->type==Div) *this=move(*a->a);  //1/1/a = a
        else if(a->type==Integer && (a->integer==1 || a->integer==-1)) *this= a->integer; //1/1 = 1
    } else if(type==Integer||type==Symbol) {}
    else fail();
    invariant();
    /// Reduce out contract invariants
    if(type==Mul) if(a->type==Integer) assert(a->integer!=1,before,*this);
    debug( if(ulp(check,toFloat())>5) { log(toString(check,24,2), before); log(toString(toFloat(),24,2),*this); log("ULP distance"_,ulp(check,toFloat())); fail(); })
}

/// Matrix for abstract linear algebra
struct Matrix {
    no_copy(Matrix)
    Matrix(Matrix&& o) : data(o.data), m(o.m), n(o.n) { o.data=0; }
    Matrix& operator=(Matrix&& o) {assert(&o!=this); this->~Matrix(); data=o.data; m=o.m; n=o.n; o.data=0; return *this; }
    /// Allocate a m-row, n-column matrix initialized to invalid expressions
    Matrix(int m, int n):data(new Expression[m*n]),m(m),n(n) {}
    Matrix(const std::initializer_list< std::initializer_list<Expression> >& list) : Matrix(list.begin()[0].size(),list.size()) {
        int i=0; for(auto& row: list) { int j=0; for(auto& e: row) { at(i,j)=Expression(::copy(e)); j++; } assert(j==n); i++; }
    }
    ~Matrix() { if(data) delete[] data; }

    const Expression& at(int i, int j) const { assert(data && i>=0 && i<m && j>=0 && j<n); return data[j*m+i]; }
    Expression& at(int i, int j) { assert(data && i>=0 && i<m && j>=0 && j<n); return data[j*m+i]; }
    const Expression& operator()(int i, int j) const { return at(i,j); }
    Expression& operator()(int i, int j) { return at(i,j); }

    Expression* data; /// elements stored in column-major order
    int m=-1,n=-1; /// row and column count
};
Matrix copy(const Matrix& a) { Matrix o(a.m,a.n);  for(int i=0;i<a.m*a.n;i++) o.data[i]=copy(a.data[i]); return o; }

/// Vector for abstract linear algebra (i.e n-row, single column matrix)
struct Vector : Matrix {
    Vector(Matrix&& o):Matrix(move(o)){ assert(n==1); }
    Vector(int n):Matrix(n,1){}
    Vector(const std::initializer_list<Expression>& list) : Vector(list.size()) {
        int i=0; for(auto& e: list) { at(i,0)=Expression(::copy(e)); i++; }
    }
    const Expression& operator[](int i) const { assert(data && i>=0 && i<m); return data[i]; }
    Expression& operator[](int i) { assert(data && i>=0 && i<m); return data[i]; }
};

/// Returns true if both matrices are identical
bool operator==(const Matrix& a,const Matrix& b) {
    assert(a.m==b.m && a.n==b.n);
    for(int i=0;i<a.m;i++) for(int j=0;j<a.n;j++) if(a(i,j)!=b(i,j)) return false;
    return true;
}
bool operator!=(const Matrix& a,const Matrix& b) { return !(a==b); }

/// Matrix multiplication (composition of linear transformations)
Matrix operator*(const Matrix& a,const Matrix& b) {
    assert(a.n==b.m);
    Matrix r(a.m,b.n);
    for(int i=0;i<r.m;i++) for(int j=0;j<r.n;j++) { r(i,j)=0; for(int k=0;k<a.n;k++) r(i,j) += a(i,k)*b(k,j); }
    return r;
}

/// Logs to standard text output
template<> void log_(const Matrix& a) {
    string s="["_;
    for(int i=0;i<a.m;i++) {
        if(a.n==1) s = s+"\t"_+a(i,0).str();
        else {
            for(int j=0;j<a.n;j++) {
                s = s+"\t"_+a(i,j).str();
            }
            if(i<a.m-1) s=s+"\n"_;
        }
    }
    s=s+" ]"_;
    log_(s);
}
template<> void log_(const Vector& a) { log_<Matrix>(a); }

/// sparse permutation matrix
struct Permutation {
    move_only(Permutation)
    int even=1; //1 if even count of swaps, -1 if odd count of swaps (used for determinant)
    array<int> order;
    Permutation(int n):order(n) { order.size=n; for(int i=0;i<n;i++) order[i] = i; } // identity ordering
    void swap(int i, int j) { ::swap(order[i],order[j]); even=-even; }
    int determinant() const { return even; }
    int operator[](int i) const { return order[i]; } //b[P[i]] = (P*b)[i]
};

Matrix operator *(const Permutation& P, Matrix&& A) {
    assert(P.order.size==A.m && P.order.size==A.n);
    Matrix PA(A.m,A.n);
    for(int i=0;i<A.m;i++) for(int j=0;j<A.n;j++) PA(P[i],j)=move(A(i,j));
    return PA;
}

// Swap row j with the row having the largest value on column j, while maintaining a permutation matrix P
void pivot(Matrix &A, Permutation& P, int j) {
    int best=j;
    for(int i = j;i<A.m;i++) { // Find biggest on or below diagonal
        if(abs(A(i,j))>abs(A(best,j))) best=i;
    }
    assert(A(best,j),A);
    if(best != j) { //swap rows i <-> j
        for(int k=0;k<A.n;k++) swap(A(best,k),A(j,k));
        P.swap(best,j);
    }
}

/// convenience macro to emulate multiple return arguments
#define multi(A, B, F) auto A##B_ F auto A = move(A##B_.A); auto B=move(A##B_.B);

/// Factorizes any matrix as the product of a lower triangular matrix and an upper triangular matrix
/// \return permutations (P) and packed LU (U's diagonal is 1).
struct PLU { Permutation P; Matrix LU; };
PLU factorize(Matrix&& A) {
    assert(A.m==A.n);
    int n = A.n;
    Permutation P(n);
    // pivot first column
    pivot(A, P, 0);
    Expression d = 1/A(0,0); for(int i=1;i<n;i++) A(0,i) *= d;
    // compute an L column, pivot to interchange rows, compute an U row.
    for (int j=1;j<n-1;j++) {
        for(int i=j;i<n;i++) { // L column
            Expression sum=0;
            for(int k=0;k<j;k++) sum += A(i,k)*A(k,j);
            A(i,j) -= sum;
        }
        pivot(A, P, j); //pivot
        Expression d = 1/A(j,j);
        for(int k=j+1;k<n;k++) { //U row
            Expression sum=0;
            for(int i=0; i<j; i++) sum += A(j,i)*A(i,k);
            A(j,k) = (A(j,k)-sum)*d;
        }
    }
    // compute last L element
    Expression sum=0;
    for(int k=0;k<n-1;k++) sum += A(n-1,k)*A(k,n-1);
    A(n-1,n-1) -= sum;
    return { move(P), move(A) };
}

/// Compute determinant of a packed PLU matrix (product along diagonal)
Expression determinant(const Permutation& P, const Matrix& LU) {
    Expression det=P.determinant();
    for(int i=0;i<LU.n;i++) det *= LU(i,i);
    return det;
}

/// Solves PLUx=b
Vector solve(const Permutation& P, const Matrix &LU, Vector&& b) {
    assert(determinant(P,LU),"Coefficient matrix is singular"_);
    int n=LU.n;
    Vector x(n);
    for(int i=0;i<n;i++) x[i] = copy(b[P[i]]); // Reorder b in x
    for(int i=0;i<n;i++) { // Forward substitution from packed L
        for(int j=0;j<i;j++) x[i] -= LU(i,j)*x[j];
        x[i] = x[i]*(1/LU(i,i));
    }
    for(int i=n-2;i>=0;i--) { // Backward substition from packed U
        for (int j=i+1; j<n; j++) x[i] -= LU(i,j) * x[j];
        //implicit ones on diagonal -> no division
    }
    return x;
}

/// Returns L and U from a packed LU matrix.
struct LU { Matrix L,U; };
LU unpack(Matrix&& LU) {
    assert(LU.m==LU.n);
    Matrix L = move(LU);
    Matrix U(L.m,L.n);
    for(int i=0;i<L.m;i++) {
        for(int j=0;j<i;j++) U(i,j)=0;
        U(i,i)=1;
        for(int j=i+1;j<L.n;j++) U(i,j)=move(L(i,j)), L(i,j)=0;
    }
    return { move(L), move(U) };
}

/// Solves Ax[j]=e[j] using LU factorization
Matrix inverse(const Matrix &A) {
    log(A);
    multi(P,LU, = factorize(copy(A)); ) //compute P,LU
    log(determinant(P,LU));
    //log(P.order);
    multi(L,U, = unpack(copy(LU)); ) //unpack LU -> L,U
    //log(L);
    //log(U);
    if(A!=P*(L*U)) log(A),log(P*(L*U)),assert(A==P*(L*U));
    int n = A.n;
    Matrix A_1(n,n);
    for(int j=0;j<n;j++) {
        Vector e(n); for(int i=0;i<n;i++) e[i]=0; e[j]=1;
        Vector x = solve(P,LU,move(e));
        for(int i=0;i<n;i++) A_1(i,j) = move(x[i]);
    }
    log(A_1);
    Matrix I(n,n); for(int i=0;i<n;i++) { for(int j=0;j<n;j++) I(i,j)=0; I(i,i)=1; }
    assert(A_1*A==I,A_1*A);
    log(pointer<Expression>::count); //check move semantics performance
    return A_1;
}

/// Solves Ax=b using LU factorization
Vector solve(const Matrix& A, const Vector& b) {
    log(A);
    multi(P,LU, = factorize(copy(A)); ) //compute P,LU
    log(determinant(P,LU));
    multi(L,U, = unpack(copy(LU)); ) //unpack LU -> L,U
    log(L);
    log(U);
    if(A!=P*(L*U)) log(A),log(L*U),log(P*(L*U)),assert(A==P*(L*U));
    log(b);
    Vector x = solve(P,LU,copy(b));
    log(x);
    log(pointer<Expression>::count); //check move semantics performance
    return x;
}

struct Test : Application {
    Test() {
        solve({{4, -2, 1}, {-3, -1, 4}, {1, -1, 3}}, {15,8,13});
        solve({{6, 1, -6, -5}, {2, 2, 3, 2}, {4, -3, 0, 1}, {0, 2,  0,  1}},{6,-2,-7,0});
        solve({{2, 1, 1, -2}, {4, 0, 2, 1}, {3, 2, 2, 0}, {1, 3,  2,  0}},{0,8,7,3});
        //solve({{3, 2, -1, -4}, {1, -1, 3, -1}, {2, 1, -3, 0}, {0, -1,  8,  -5}},{10,-4,16,3}); //singular
        const string &a = "a"__, &b="b"__;
        inverse({{a, a/2}, {0, b}});
    }
} test;

