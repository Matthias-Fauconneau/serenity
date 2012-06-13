#pragma once
#include "string.h"
#include "debug.h"

template<class T> T sq(const T& x) { return x*x; }
template<class T> T cb(const T& x) { return x*x*x; }

extern struct Zero {} zero; //dummy type to call zero-initializing constructor
//TODO: SIMD
template<template<typename> class V, class T, int N> struct vector : V<T> {
    static const int size = N;
    //vector()debug(:V<T>{}){}
    vector():V<T>{}{}
    vector(Zero):V<T>{}{}
    template<class... Args> explicit vector(Args... args):V<T>{args...}{static_assert(sizeof...(args) == N, "Invalid number of arguments");}
    template<class T2> explicit vector(const vector<V,T2,N>& o) { for(int i=0;i<N;i++) u(i)=(T)o[i]; }
    const T& u(int i) const { return ((T*)this)[i]; }
    T& u(int i) { return ((T*)this)[i]; }
    const T& operator[](int i) const { assert(i>=0 && i<N); return u(i); }
    T& operator[](int i) { assert(i>=0 && i<N); return u(i); }
    vector operator +=(vector v);
    vector operator -=(vector v);
    vector operator *=(vector v);
    vector operator *=(float s);
    vector operator /=(float s);
    vector operator -() const;
    vector operator +(vector v) const;
    vector operator -(vector v) const;
    vector operator *(vector v) const;
    vector operator *(float s) const;
    vector operator /(float s) const;
    bool operator ==(vector v) const;
    bool operator !=(vector v) const;
    bool operator >(vector v) const;
    bool operator <(vector v) const;
    bool operator >=(vector v) const;
    bool operator <=(vector v) const;
    explicit operator bool() const;
};

#define generic template<template<typename> class V, class T, int N>
generic vector<V,T,N> operator *(float s, vector<V,T,N> v);
generic vector<V,T,N> abs(vector<V,T,N> v);
generic vector<V,T,N> sign(vector<V,T,N> v);
generic vector<V,T,N> min(vector<V,T,N> a, vector<V,T,N> b);
generic vector<V,T,N> max(vector<V,T,N> a, vector<V,T,N> b);
generic vector<V,T,N> clip(T min, vector<V,T,N> x, T max);
generic float dot( vector<V,T,N> a,  vector<V,T,N> b);
generic float length( vector<V,T,N> a);
generic vector<V,T,N> normalize( vector<V,T,N> a);
generic string str( vector<V,T,N> v);
#undef generic

template<class T> struct xy { T x,y; };
typedef vector<xy,int,2> int2;
typedef vector<xy,float,2> vec2;
