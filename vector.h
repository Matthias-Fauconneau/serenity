#pragma once
#include "string.h"

/// Provides vector operations on \a N packed values of type \a T stored in struct \a V<T>
/// \note statically inheriting the data type allows to provide vector operations to new types and to access components directly
template<template<typename> class V, class T, int N> struct vector : V<T> {
    /// Initializes all components to their default values
    constexpr vector():____(V<T>{}){} //TODO: change to uninitialized for performance
    /// Initializes all components to the same value \a v
    explicit vector(T v){ for(int i=0;i<N;i++) at(i)=v; }
    /// Initializes each component separately
    template<class... Args> explicit constexpr vector(T a, T b, Args... args):____(V<T>{a,b,T(args)...}){
        static_assert(sizeof...(args) == N-2, "Invalid number of arguments");
    }
    /// Copies each component from another vector \a o casting from \a T2 to \a T
    template<class F> explicit vector(const vector<V,F,N>& o) { for(int i=0;i<N;i++) at(i)=(T)o[i]; }
    /// Accessors (always unchecked)
    const T& at(int i) const { return ((T*)this)[i]; }
    T& at(int i) { return ((T*)this)[i]; }
    /// Accessors (checked in debug build)
    const T& operator[](uint i) const { assert_(i<N); return at(i); }
    T& operator[](uint i) { assert_(i<N); return at(i); }
    /// Operators
    explicit operator bool() const { for(int i=0;i<N;i++) if(at(i)!=0) return true; return false; }
    vector& operator +=(const vector& v) { for(int i=0;i<N;i++) at(i)+=v[i]; return *this; }
    vector& operator -=(const vector& v) { for(int i=0;i<N;i++) at(i)-=v[i]; return *this; }
    vector& operator *=(const vector& v) { for(int i=0;i<N;i++) at(i)*=v[i]; return *this; }
    vector& operator /=(const T& s) { for(int i=0;i<N;i++) at(i)/=s; return *this; }
};

#define generic template <template <typename> class V, class T, int N>
#define vector vector<V,T,N>

generic vector operator -(const vector& u) { vector r; for(int i=0;i<N;i++) r[i]=-u[i]; return r; }
generic vector operator +(const vector& u, const vector& v) { vector r; for(int i=0;i<N;i++) r[i]=u[i]+v[i]; return r; }
generic vector operator -(const vector& u, const vector& v) { vector r; for(int i=0;i<N;i++) r[i]=u[i]-v[i]; return r; }
generic vector operator *(const vector& u, const vector& v) { vector r; for(int i=0;i<N;i++) r[i]=u[i]*v[i]; return r; }
generic vector operator *(const vector& u, int s) { vector r; for(int i=0;i<N;i++) r[i]=u[i]*s; return r; }
generic vector operator *(int s, const vector& u) { vector r; for(int i=0;i<N;i++) r[i]=s*u[i]; return r; }
generic vector operator /(const vector& u, int s) { vector r; for(int i=0;i<N;i++) r[i]=u[i]/s; return r; }
generic bool operator ==(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]!=v[i]) return false; return true; }
generic bool operator >(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]<=v[i]) return false; return true; }
generic bool operator <(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]>=v[i]) return false; return true; }
generic bool operator >=(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]<v[i]) return false; return true; }
generic bool operator <=(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]>v[i]) return false; return true; }

generic vector abs(const vector& v){ vector r; for(int i=0;i<N;i++) r[i]=abs(v[i]); return r;  }
generic vector min(const vector& a, const vector& b){ vector r; for(int i=0;i<N;i++) r[i]=min(a[i],b[i]); return r;  }
generic vector max(const vector& a, const vector& b){ vector r; for(int i=0;i<N;i++) r[i]=max(a[i],b[i]); return r;  }
generic vector clip(T min, const vector& x, T max){ vector r; for(int i=0;i<N;i++) r[i]=clip(min,x[i],max); return r;  }

generic float dot(const vector& a, const vector& b) { float l=0; for(int i=0;i<N;i++) l+=a[i]*b[i]; return l; }
generic float length(const vector& a) { return __builtin_sqrtf(dot(a,a)); }
generic vector normalize(const vector& a){ return a/length(a); }

template<class T> T mix(const T& a,const T& b, float t) { return a*t + b*(1-t); }

//inline float cross(vec2 a, vec2 b) { return a.y*b.x - a.x*b.y; }
//inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x); }

generic string str(const vector& v) { string s = string("("_); for(int i=0;i<N;i++) { s<<str(v[i]); if(i<N-1) s<<", "_; } s<<")"_; return s; }

#undef vector
#undef generic

template<class T> struct xy { T x,y; };
typedef vector<xy,int,2> int2;

/// Rect
struct Rect {
    int2 min,max;
    explicit Rect(int2 max):min(0,0),max(max){}
    Rect(int2 min, int2 max):min(min),max(max){}
    bool contains(int2 p) { return p>=min && p<max; }
    explicit operator bool() { return (max-min)>int2(0,0); }
    int2& position() { return min; }
    int2 size() { return max-min; }
};
inline Rect operator +(int2 offset, Rect rect) { return Rect(offset+rect.min,offset+rect.max); }
inline Rect operator |(Rect a, Rect b) { return Rect(min(a.min,b.min),max(a.max,b.max)); }
inline Rect operator &(Rect a, Rect b) { return Rect(max(a.min,b.min),min(a.max,b.max)); }
inline bool operator ==(Rect a, Rect b) { return a.min==b.min && a.max==b.max; }
