#pragma once
/// \file vector.h Vector definitions and operations
#include "string.h"
#include "math.h"
typedef short v2hi __attribute((__vector_size__ (4)));
typedef int v4si __attribute((__vector_size__(16)));
typedef float v4sf __attribute((__vector_size__(16)));
typedef int v8si __attribute((__vector_size__(32)));
typedef float v8sf __attribute((__vector_size__(32)));
template<> inline String str(const v4si& v) { return "("_+str(v[0])+", "_+str(v[1])+", "_+str(v[2])+", "_+str(v[3])+")"_; }
template<> inline String str(const v4sf& v){ return "("_+str(v[0])+", "_+str(v[1])+", "_+str(v[2])+", "_+str(v[3])+")"_; }
//template<> inline String str(const v8si& v) { return  "("_+str(v[0])+", "_+str(v[1])+", "_+str(v[2])+", "_+str(v[3])+", "_+str(v[4])+", "_+str(v[5])+", "_+str(v[6])+", "_+str(v[7])+")"_; }
//template<> inline String str(const v8sf& v) { return  "("_+str(v[0])+", "_+str(v[1])+", "_+str(v[2])+", "_+str(v[3])+", "_+str(v[4])+", "_+str(v[5])+", "_+str(v[6])+", "_+str(v[7])+")"_; }

/// Provides vector operations on \a N packed values of type \a T stored in struct \a V<T>
/// \note statically inheriting the data type allows to provide vector operations to new types and to access named components directly
template<template<typename> class V, Type T, uint N> struct vec : V<T> {
    static_assert(sizeof(V<T>)==N*sizeof(T),"");

    vec(){}
    /// Initializes all components to the same value \a v
    /*constexpr*/ vec(T v) { for(uint i=0;i<N;i++) at(i)=v; }
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
    /*constexpr*/ T operator[](uint i) const { assert(i<N); return at(i); }
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

generic vec rotate(const vec& u) { vec r=u; for(uint i=0;i<N-1;i++) swap(r[i],r[i+1]); return r; }

generic vec operator +(const vec& u) { return u; }
generic vec operator -(const vec& u) { vec r; for(uint i=0;i<N;i++) r[i]=-u[i]; return r; }
generic vec operator +(const vec& u, const vec& v) { vec r; for(uint i=0;i<N;i++) r[i]=u[i]+v[i]; return r; }
generic vec operator -(const vec& u, const vec& v) { vec r; for(uint i=0;i<N;i++) r[i]=u[i]-v[i]; return r; }
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
/// Integer x,y vector (16bit)
/*struct short2 : vec<xy,int16,2> {
    inline operator v2hi() const { return (v2hi){x,y}; }
;*/
typedef vec<xy,short,2> short2;
/// Integer x,y vector (32bit)
typedef vec<xy,int,2> int2;
/// Single precision x,y vector
typedef vec<xy,float,2> float2;
typedef vec<xy,float,2> vec2;
inline float cross(vec2 a, vec2 b) { return a.y*b.x - a.x*b.y; }
inline float cross(int2 a, int2 b) { return a.y*b.x - a.x*b.y; }
inline vec2 normal(vec2 a) { return vec2(-a.y, a.x); }

generic struct xyz {
    T x,y,z;
    vec< ::xy,T,2> xy() const { return vec< ::xy,T,2>(x,y); }
    inline operator v4sf() const { return (v4sf){x,y,z,0}; }
};
/// Integer x,y,z vector
typedef vec<xyz,int,3> int3;
/// Unsigned integer x,y,z vector
typedef vec<xyz,uint,3> uint3;
/// Integer x,y,z vector (16bit)
typedef vec<xyz,uint16,3> short3;
/// Floating-point x,y,z vector
typedef vec<xyz,float,3> vec3;
inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y*b.z - b.y*a.z, a.z*b.x - b.z*a.x, a.x*b.y - b.x*a.y); }
inline vec3 normal(vec3 v) {
    int index=0; float min=v[0];
    for(int i: range(3)) if(abs(v[i]) < min) index=i, min=abs(v[i]);
    vec3 t=0; t[index]=1;
    return normalize(cross(v, t));
}

generic struct xyzw {
    T x,y,z,w;
    constexpr xyzw() {}
    vec< ::xyz,T,3> xyz() const { return *(vec< ::xyz,T,3>*)this; }
    vec< ::xyz,T,3> xyw() const { return vec< ::xyz,T,3>(x,y,w); }
    vec< ::xy,T,2> xy()const{ return *(vec< ::xy,T,2>*)this; }
    inline operator v4sf() const { return *(v4sf*)this; }
};
/// Floating-point x,y,z,w vector
typedef vec<xyzw,float,4> vec4;

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
