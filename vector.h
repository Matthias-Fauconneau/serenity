#pragma once
#include "core.h"

/// Mathematic primitives

template<class T> inline T clip(T min, T x, T max) { return x < min ? min : x > max ? max : x; }
/*
inline int floor(float f) { return __builtin_floorf(f); }
inline int round(float f) { return __builtin_roundf(f); }
inline int ceil(float f) { return __builtin_ceilf(f); }

const double PI = 3.14159265358979323846;
inline float sin(float t) { return __builtin_sinf(t); }
inline float sqrt(float f) { return __builtin_sqrtf(f); }
inline float atan(float f) { return __builtin_atanf(f); }

template<class T> T sq(const T& x) { return x*x; }
template<class T> T cb(const T& x) { return x*x*x; }

/// SIMD
typedef float float4 __attribute__ ((vector_size(16)));
typedef double double2 __attribute__ ((vector_size(16)));
#define xor_ps __builtin_ia32_xorps
#define xor_pd __builtin_ia32_xorpd
#define loadu_ps __builtin_ia32_loadups
#define loadu_pd __builtin_ia32_loadupd
#define loada_ps(e) (*(float4*)(e))
#define movehl_ps __builtin_ia32_movhlps
#define shuffle_ps __builtin_ia32_shufps
#define extract_s __builtin_ia32_vec_ext_v4sf
#define extract_d __builtin_ia32_vec_ext_v2df
*/

extern struct Zero {} zero; //dummy type to call zero-initializing constructor
template<template<typename> class V, class T, int N> struct vector : V<T> {
    static const int size = N;
    vector():V<T>{}{}
    vector(Zero):V<T>{}{}
    template<class... Args> explicit vector(T x, T y, Args... args):V<T>{x,y,args...}{
        static_assert(sizeof...(args) == N-2, "Invalid number of arguments");
    }
    template<class T2> explicit vector(const vector<V,T2,N>& o) { for(int i=0;i<N;i++) u(i)=(T)o[i]; }
    const T& u(int i) const { return ((T*)this)[i]; }
    T& u(int i) { return ((T*)this)[i]; }
    const T& operator[](uint i) const { assert_(i<N); return u(i); }
    T& operator[](uint i) { assert_(i<N); return u(i); }
    vector operator +=(vector v);
    vector operator -=(vector v);
    vector operator *=(vector v);
    //vector operator *=(float s);
    //vector operator /=(float s);
    vector operator -() const;
    vector operator +(vector v) const;
    vector operator -(vector v) const;
    vector operator *(vector v) const;
    vector operator *(int s) const;
    vector operator /(uint s) const;
    bool operator ==(vector v) const;
    bool operator !=(vector v) const;
    bool operator >(vector v) const;
    bool operator <(vector v) const;
    bool operator >=(vector v) const;
    bool operator <=(vector v) const;
    explicit operator bool() const;
};

#define generic template<template<typename> class V, class T, int N>
generic vector<V,T,N> operator *(int s, vector<V,T,N> v);
generic vector<V,T,N> abs(vector<V,T,N> v);
generic vector<V,T,N> sign(vector<V,T,N> v);
generic vector<V,T,N> min(vector<V,T,N> a, vector<V,T,N> b);
generic vector<V,T,N> max(vector<V,T,N> a, vector<V,T,N> b);
generic vector<V,T,N> clip(T min, vector<V,T,N> x, T max);
//generic float dot( vector<V,T,N> a,  vector<V,T,N> b);
//generic float length( vector<V,T,N> a);
generic vector<V,T,N> normalize( vector<V,T,N> a);
struct string;
generic string str( vector<V,T,N> v);
#undef generic

template<class T> struct xy { T x,y; };
typedef vector<xy,int,2> int2;
//typedef vector<xy,float,2> vec2;
