#pragma once
#include "string.h"
#include <math.h>

/// unique string literals (unique strings stay allocated for the whole program and can be tested for equality using pointer comparison)
inline const string& operator "" __(const char* data, size_t size) {
    static array<string*> uniques; //TODO: user defined namespaces
    string unique(data,size);
    int i = uniques.find([&unique](string* s){return *s==unique;});
    if(i<0) { i=uniques.size; uniques<<new string(move(unique)); }
    return *uniques[i];
}

/// pointer to a pool-allocated value with move semantics
template<class T> struct pointer {
    no_copy(pointer)
    pointer(){}
    static const int max=65536; //Maximum expression complexity //TODO: avoid copying
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

// Expression type
enum Type { Invalid,    Operand/*:*/, Symbol, Integer,   Unary/*:*/, Sub, Div,    Binary/*:*/, Add, Mul, Pow };
/// Represents abstract mathematical expression using a binary tree of operations
struct Expression {
    Type type=Invalid;
    int integer=0;
    const string* symbol=0;
    pointer<Expression> a, b;

    /// Move semantics allow to manage memory without reference counting nor garbage collection
    move_only(Expression)

    Expression() {} /// Default constructs an invalid Expression
    Expression(int integer) : type(Integer), integer(integer) {} /// Literal integer
    Expression(const string& symbol) : type(Symbol), symbol(addressof(symbol)) {} /// Symbol /// \note \a symbol should outlive this \a Expression
    Expression(Type type, pointer<Expression>&& a) : type(type), a(move(a)) { /// Unary operator
        assert(type==Sub||type==Div); reduce();
    }
    Expression(Type type, pointer<Expression>&& a, pointer<Expression>&& b) : type(type), a(move(a)), b(move(b)) { /// Binary operator
        assert(type==Add||type==Mul||type==Pow); reduce();
    }
    /// Verify if expression is well formed
    void invariant() const;
    /// Returns true if the expression contains an occurence of e (also search operands recursively)
    bool contains(const Expression& e) const;
    /// Returns a readable formatted representation
    string str() const;
    /// Returns an approximation as a floating point (symbols are evaluated to 1)
    float toFloat();
    /// Performs pattern matching to simplify the expression (should be called after editing operands)
    void reduce();
    /// Tests if the expression evaluates to zero
    explicit operator bool() const {
        if(type==Integer) return integer;
        //FIXME: symbols?
        return true; //not zero
    }
};
template<> inline Expression copy(const Expression& e) {
    Expression r; r.type=e.type; r.integer=e.integer; r.symbol=e.symbol; r.a=copy(e.a); r.b=copy(e.b); return r;
}
template<> inline string str(const Expression& a) { return str(a.str()); }

//TODO: perfect forwarding to handle mixed references (move/move,move/copy,copy/move,copy/copy)
/// move semantics
inline Expression operator-(pointer<Expression>&& a) { return Expression(Sub,move(a)); }
inline Expression operator/(const int a, pointer<Expression>&& b) { assert(a==1); return Expression(Div,move(b)); }

inline Expression operator+(pointer<Expression>&& a, pointer<Expression>&& b) { return Expression(Add,move(a),move(b)); }
inline Expression operator*(pointer<Expression>&& a, pointer<Expression>&& b) { return Expression(Mul,move(a),move(b)); }
inline Expression operator^(pointer<Expression>&& a, pointer<Expression>&& b) { return Expression(Pow,move(a),move(b)); }

inline Expression operator-(pointer<Expression>&& a, pointer<Expression>&& b) { return move(a)+-move(b); }
inline Expression operator/(pointer<Expression>&& a, pointer<Expression>&& b) { return move(a)*(1/move(b)); }

/// copy semantics
#define new(a) pointer<Expression>(copy(a))
inline Expression operator-(const Expression& a) { return -new(a); }
inline Expression operator/(const int a, const Expression& b) { assert(a==1); return 1/new(b); }

inline Expression operator+(const Expression& a, const Expression& b) { return new(a)+new(b); }
inline Expression operator*(const Expression& a, const Expression& b) { return new(a)*new(b); }
inline Expression operator^(const Expression& a, const Expression& b) { return new(a)^new(b); }

inline Expression operator-(const Expression& a, const Expression& b) { return new(a)+-new(b); }
inline Expression operator/(const Expression& a, const Expression& b) { return new(a)*(1/new(b)); }

inline Expression& operator+=(Expression& a, const Expression& b) { return a=a+b; }
inline Expression& operator-=(Expression& a, const Expression& b) { return a=a-b; }
inline Expression& operator*=(Expression& a, const Expression& b) { return a=a*b; }
inline Expression& operator/=(Expression& a, const Expression& b) { return a=a/b; }

Expression abs(const Expression& a);

/// comparison operators
bool operator==(const Expression& a, const Expression& b);
inline bool operator!=(const Expression& a, const Expression& b) { return !(a==b); }

bool operator>(const Expression& a, int integer);
inline bool operator>(const Expression& a, const Expression& b) { return (a-b)>0; }
