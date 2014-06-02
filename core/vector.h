#pragma once
/// \file vector.h Vector definitions and operations
#include "string.h"
#include "math.h"

/// Provides vector operations on \a N packed values of type \a T stored in struct \a V<T>
/// \note statically inheriting the data type allows to provide vector operations to new types and to access named components directly
template<template<typename> class V, Type T, uint N> struct vec : V<T> {
    static_assert(sizeof(V<T>)==N*sizeof(T),"");

    vec(){}
    /// Initializes all components to the same value \a v
    constexpr vec(T v) { for(uint i=0;i<N;i++) at(i)=v; }
    /// Initializes components separately
    template<Type... Args> constexpr vec(T a, T b, Args... args):V<T>{a,b,T(args)...}{
        static_assert(sizeof...(args) == N-2, "Invalid number of arguments");
    }
    /// Initializes components from a fixed size array
    template<Type... Args> explicit vec(const T o[N]){ for(uint i=0;i<N;i++) at(i)=(T)o[i]; }
    /// Initializes first components from another vec \a o and initializes remaining components with args...
    template<template<typename> class W, Type... Args> vec(const vec<W,T,N-sizeof...(Args)>& o, Args... args){
        for(int i: range(N-sizeof...(Args))) at(i)=o[i];
        T unpacked[]={T(args)...}; for(int i: range(sizeof...(Args))) at(N-sizeof...(Args)+i)=unpacked[i];
    }
    /// Initializes components from another vec \a o casting from \a T2 to \a T
    template<template<typename> class W, Type F> explicit vec(const vec<W,F,N>& o) { for(uint i=0;i<N;i++) at(i)=(T)o[i]; }

    /// Unchecked accessor (const)
    const T& at(uint i) const { return ((T*)this)[i]; }
    /// Unchecked accessor
    T& at(uint i) { return ((T*)this)[i]; }
    /// Accessor (checked in debug build, const)
    constexpr T operator[](uint i) const { assert(i<N); return at(i); }
    /// Accessor (checked in debug build)
    T& operator[](uint i) { assert(i<N); return at(i); }
    /// \name Operators
    explicit operator bool() const { for(uint i=0;i<N;i++) if(at(i)!=0) return true; return false; }
    vec& operator +=(const vec& v) { for(uint i=0;i<N;i++) at(i)+=v[i]; return *this; }
    vec& operator -=(const vec& v) { for(uint i=0;i<N;i++) at(i)-=v[i]; return *this; }
    vec& operator *=(const vec& v) { for(uint i=0;i<N;i++) at(i)*=v[i]; return *this; }
    vec& operator *=(const T& s) { for(uint i=0;i<N;i++) at(i)*=s; return *this; }
    vec& operator /=(const T& s) { for(uint i=0;i<N;i++) at(i)/=s; return *this; }
    /// \}
};

#undef generic
#define generic template <template <typename> class V, Type T, uint N>
#define vec vec<V,T,N>

generic vec operator +(const vec& u) { return u; }
generic vec operator -(const vec& u) { vec r; for(uint i=0;i<N;i++) r[i]=-u[i]; return r; }
generic vec operator +(const vec& u, const vec& v) { vec r; for(uint i=0;i<N;i++) r[i]=u[i]+v[i]; return r; }
generic vec operator +(const vec& u, T s) { vec r; for(uint i=0;i<N;i++) r[i]=u[i]+s; return r; }
generic vec operator -(const vec& u, const vec& v) { vec r; for(uint i=0;i<N;i++) r[i]=u[i]-v[i]; return r; }
generic vec operator -(const vec& u, T s) { vec r; for(uint i=0;i<N;i++) r[i]=u[i]-s; return r; }
generic vec operator *(const vec& u, const vec& v) { vec r; for(uint i=0;i<N;i++) r[i]=u[i]*v[i]; return r; }
generic vec operator *(const vec& u, T s) { vec r; for(uint i=0;i<N;i++) r[i]=u[i]*s; return r; }
generic vec operator *(T s, const vec& u) { vec r; for(uint i=0;i<N;i++) r[i]=s*u[i]; return r; }
generic vec operator /(T s, const vec& u) { vec r; for(uint i=0;i<N;i++) r[i]=s/u[i]; return r; }
generic vec operator /(const vec& u, T s) { vec r; for(uint i=0;i<N;i++) r[i]=u[i]/s; return r; }
generic vec operator /(const vec& u, const vec& v) { vec r; for(uint i=0;i<N;i++) r[i]=u[i]/v[i]; return r; }
generic bool operator <(const vec& u, const vec& v) { for(uint i=0;i<N;i++) if(u[i]>=v[i]) return false; return true; }
generic bool operator <=(const vec& u, const vec& v) { for(uint i=0;i<N;i++) if(u[i]>v[i]) return false; return true; }
generic bool operator ==(const vec& u, const vec& v) { for(uint i=0;i<N;i++) if(u[i]!=v[i]) return false; return true; }
generic bool operator >=(const vec& u, const vec& v) { for(uint i=0;i<N;i++) if(u[i]<v[i]) return false; return true; }
generic bool operator >(const vec& u, const vec& v) { for(uint i=0;i<N;i++) if(u[i]<=v[i]) return false; return true; }

generic vec abs(const vec& v){ vec r; for(uint i=0;i<N;i++) r[i]=abs(v[i]); return r;  }
generic vec sign(const vec& v){ vec r; for(uint i=0;i<N;i++) r[i]=ceil(v[i]); return r;  }
generic vec floor(const vec& v){ vec r; for(uint i=0;i<N;i++) r[i]=floor(v[i]); return r;  }
generic vec fract(const vec& v){ vec r; for(uint i=0;i<N;i++) r[i]=mod(v[i],1); return r;  }
generic vec round(const vec& v){ vec r; for(uint i=0;i<N;i++) r[i]=round(v[i]); return r;  }
generic vec ceil(const vec& v){ vec r; for(uint i=0;i<N;i++) r[i]=ceil(v[i]); return r;  }
generic vec min(const vec& a, const vec& b){ vec r; for(uint i=0;i<N;i++) r[i]=min(a[i],b[i]); return r; }
generic vec max(const vec& a, const vec& b){ vec r; for(uint i=0;i<N;i++) r[i]=max(a[i],b[i]); return r; }
generic vec clip(const vec& min, const vec& x, const vec& max){vec r; for(uint i=0;i<N;i++) r[i]=clip(min[i],x[i],max[i]); return r;}

generic float dot(const vec& a, const vec& b) { float l=0; for(uint i=0;i<N;i++) l+=a[i]*b[i]; return l; }
generic float sq(const vec& a) { return dot(a,a); }
generic float norm(const vec& a) { return sqrt(dot(a,a)); }
generic vec normalize(const vec& a){ return a/norm(a); }
generic bool isNaN(const vec& v){ for(uint i=0;i<N;i++) if(isNaN(v[i])) return true; return false; }

generic String str(const vec& v) { String s("("_); for(uint i=0;i<N;i++) { s<<str(v[i]); if(i<N-1) s<<", "_; } s<<")"_; return s; }

#undef vec
#undef generic
#define generic template<Type T>

generic struct xy {
    T x,y;
};
/// Integer x,y vector (32bit)
typedef vec<xy,int,2> int2;
/// Single precision x,y vector
typedef vec<xy,float,2> float2;
typedef float2 vec2;

generic struct xyz {
    T x,y,z;
    vec< ::xy,T,2> xy() const { return vec< ::xy,T,2>(x,y); }
};
/// Integer x,y,z vector
typedef vec<xyz,int,3> int3;
/// Floating-point x,y,z vector
typedef vec<xyz,float,3> float3;
typedef float3 vec3;

generic struct xyzw {
    T x,y,z,w;
    constexpr xyzw() {}
    vec< ::xyz,T,3> xyz() const { return *(vec< ::xyz,T,3>*)this; }
    vec< ::xyz,T,3> xyw() const { return vec< ::xyz,T,3>(x,y,w); }
    vec< ::xy,T,2> xy()const{ return *(vec< ::xy,T,2>*)this; }
};
/// Floating-point x,y,z,w vector
typedef vec<xyzw,float,4> float4;

generic struct bgr {
    T b,g,r;
};
/// Integer b,g,r vector (8bit)
typedef vec<bgr,uint8,3> byte3;

generic struct bgra {
    T b,g,r,a;
    vec< ::bgr,T,3>& bgr() const { return *(vec< ::bgr,T,3>*)this; }
};
/// Integer b,g,r,a vector (8bit)
typedef vec<bgra,uint8,4> byte4;
/// Integer b,g,r,a vector (32bit)
typedef vec<bgra,int,4> int4;
