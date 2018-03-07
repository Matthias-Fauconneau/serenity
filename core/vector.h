#pragma once
/// \file vector.h Vector definitions and operations
#include "string.h"

/// Provides vector operations on \a N packed values of type \a T stored in struct \a V<T>
/// \note statically inheriting the data type allows to provide vector operations to new types and to access named components directly
template<template<Type> Type V, Type _T, uint _N> struct vec : V<_T> {
    generic using _V = V<T>;
    typedef _T T;
    static constexpr uint N = _N;
    static_assert(sizeof(V<T>)==N*sizeof(T));

    /// Defaults initializes to zero
    inline constexpr vec() /*: vec(0)*/ {}
    /// Initializes all components to the same value \a v
    inline vec(T v) { for(uint i: range(N)) at(i)=v; }
    /// Initializes components separately
    template<Type... Args> inline /*explicit*/ constexpr vec(T a, T b, Args... args) : V<T>{a,b,T(args)...} {
        static_assert(sizeof...(args) == N-2, "Invalid number of arguments");
    }
    /// Initializes components from a fixed size array
    template<Type... Args> inline explicit vec(const T o[N]){ for(uint i: range(N)) at(i)=(T)o[i]; }

    /// Initializes components from another vec \a o casting from \a S to \a T
    template<Type S> inline explicit vec(const vec<V,S,N>& o) { for(uint i: range(N)) at(i)=(T)o[i]; }

    /// Initializes first components from another vec \a o and initializes remaining components with args...
    template<template<Type> Type W, Type... Args> vec(const vec<W,T,N-sizeof...(Args)>& o, Args... args){
        for(int i: range(N-sizeof...(Args))) at(i)=o[i];
        T unpacked[]={T(args)...}; for(int i: range(sizeof...(Args))) at(N-sizeof...(Args)+i)=unpacked[i];
    }

    operator ref<T>() const { return ref<T>(reinterpret_cast<const T*>(this), N); }

    /// \name Accessors
    inline const T& at(uint i) const { return reinterpret_cast<const T*>(this)[i]; }
    inline T& at(uint i) { return ((T*)this)[i]; }
    inline const T& operator[](uint i) const { return at(i); }
    inline T& operator[](uint i) { return at(i); }

    /// \name Operators
    explicit operator bool() const { for(uint i: range(N)) if(at(i)!=0) return true; return false; }
    inline vec& operator +=(const vec& v) { for(uint i: range(N)) at(i)+=v[i]; return *this; }
    vec& operator -=(const vec& v) { for(uint i: range(N)) at(i)-=v[i]; return *this; }
    vec& operator *=(const vec& v) { for(uint i: range(N)) at(i)*=v[i]; return *this; }
    vec& operator *=(const T& s) { for(uint i: range(N)) at(i)*=s; return *this; }
    vec& operator /=(const T& s) { for(uint i: range(N)) at(i)/=s; return *this; }
    /// \}
};

#define genericVec template<template<Type> Type V, Type T, uint N> static inline constexpr
#define Vec vec<V,T,N>

genericVec Vec rotate(const Vec& u) { Vec r=u; for(uint i: range(N-1)) swap(r[i],r[i+1]); return r; }

genericVec Vec operator +(const Vec& u) { return u; }
genericVec Vec operator -(const Vec& u) { Vec r; for(uint i: range(N)) r[i]=-u[i]; return r; }
genericVec Vec operator +(const Vec& u, const Vec& v) { Vec r; for(uint i: range(N)) r[i]=u[i]+v[i]; return r; }
genericVec Vec operator -(const Vec& u, const Vec& v) { Vec r; for(uint i: range(N)) r[i]=u[i]-v[i]; return r; }
genericVec Vec operator *(const Vec& u, const Vec& v) { Vec r; for(uint i: range(N)) r[i]=u[i]*v[i]; return r; }
genericVec Vec operator *(const Vec& u, T s) { Vec r; for(uint i: range(N)) r[i]=u[i]*s; return r; }
genericVec Vec operator *(T s, const Vec& u) { Vec r; for(uint i: range(N)) r[i]=s*u[i]; return r; }
genericVec Vec operator /(T s, const Vec& u) { Vec r; for(uint i: range(N)) r[i]=s/u[i]; return r; }
genericVec Vec operator /(const Vec& u, T s) { Vec r; for(uint i: range(N)) r[i]=u[i]/s; return r; }
genericVec Vec operator /(const Vec& u, const Vec& v) { Vec r; for(uint i: range(N)) r[i]=u[i]/v[i]; return r; }
genericVec auto operator <(const Vec& u, const Vec& v) { vec<V,decltype(T()<T()),N> r; for(uint i: range(N)) r[i] = u[i] < v[i]; return r; }
genericVec bool operator <=(const Vec& u, const Vec& v) { for(uint i: range(N)) if(u[i]>v[i]) return false; return true; }
genericVec bool operator ==(const Vec& u, const Vec& v) { for(uint i: range(N)) if(u[i]!=v[i]) return false; return true; }
genericVec bool operator >=(const Vec& u, const Vec& v) { for(uint i: range(N)) if(u[i]<v[i]) return false; return true; }
genericVec bool operator >(const Vec& u, const Vec& v) { for(uint i: range(N)) if(u[i]<=v[i]) return false; return true; }

genericVec Vec abs(const Vec& v) { Vec r; for(uint i: range(N)) r[i]=abs(v[i]); return r;  }
genericVec Vec floor(const Vec& v) { Vec r; for(uint i: range(N)) r[i]=floor(v[i]); return r;  }
genericVec Vec fract(const Vec& v) { Vec r; for(uint i: range(N)) r[i]=fract(v[i]); return r;  }
genericVec Vec round(const Vec& v) { Vec r; for(uint i: range(N)) r[i]=__builtin_roundf(v[i]); return r;  }
genericVec Vec ceil(const Vec& v) { Vec r; for(uint i: range(N)) r[i]=__builtin_ceilf(v[i]); return r;  }
genericVec Vec min(const Vec& a, const Vec& b) { Vec r; for(uint i: range(N)) r[i]=min(a[i],b[i]); return r; }
genericVec Vec max(const Vec& a, const Vec& b) { Vec r; for(uint i: range(N)) r[i]=max(a[i],b[i]); return r; }
genericVec Vec clamp(const Vec& min, const Vec& x, const Vec& max) { Vec r; for(uint i: range(N)) r[i]=clamp(min[i],x[i],max[i]); return r;}

genericVec T min(const Vec& v) { return min((ref<T>)v); }
genericVec T hsum(const Vec& a) { T sum=0; for(uint i: range(N)) sum+=a[i]; return sum; }
genericVec T product(const Vec& a) { T product=1; for(uint i: range(N)) product *= a[i]; return product; }
genericVec T dot(const Vec& a, const Vec& b) { T ssq=0; for(uint i: range(N)) ssq += a[i]*b[i]; return ssq; }
//genericVec T sq(const Vec& a) { return dot(a,a); }
genericVec float length(const Vec& a) { return __builtin_sqrtf(dot(a, a)); }
genericVec Vec normalize(const Vec& a) { return a/length(a); }
genericVec bool isNaN(const Vec& v) { for(uint i: range(N)) if(isNaN(v[i])) return true; return false; }
genericVec bool isNumber(const Vec& v) { for(uint i: range(N)) if(v[i]!=v[i] || v[i] == __builtin_inff() || v[i] == -__builtin_inff()) return false; return true; }

template<template<Type> Type V, Type T, uint N> String str(const Vec& v) {
    buffer<char> s(16*N, 0);
    s.append('(');
    for(uint i: range(N)) { s.append(str(v[i],2u)); if(i<N-1) s.append(" "); }
    s.append(')');
    return move(s);
}

generic struct x { T x; };

generic struct xy {
    T x, y;
    vec< ::xy, T, 2> yx() const { return vec< ::xy, T, 2>{y,x}; }
};
typedef vec<xy,uint8,2> byte2;
typedef vec<xy,int32,2> int2;
typedef vec<xy,int64,2> int64x2;
typedef vec<xy,uint32,2> uint2;
typedef vec<xy,float,2> vec2;

generic struct xyz {
    T x,y,z;
    vec<xy,T,2>& xy() const { return (vec< ::xy,T,2>&)*this; }
};
/// Integer x,y,z vector
typedef vec<xyz,int32,3> int3;
typedef vec<xyz,int64,3> int64x3;
typedef vec<xyz,uint32,3> uint3;
typedef vec<xyz,float,3> vec3;

generic struct xyzw {
    T x,y,z,w;
    vec< ::xy,T,2> xy() const { return *(vec< ::xy,T,2>*)this; }
    const vec< ::xyz,T,3> xyz() const { return *(vec< ::xyz,T,3>*)this; }
    vec< ::xyz,T,3>& xyz() { return *(vec< ::xyz,T,3>*)this; }
    vec< ::xyz,T,3> xyw() const { return vec3(x, y, w); }
};
typedef vec<xyzw,float,4> vec4;
typedef vec<xyzw,int32,4> int4;
typedef vec<xyzw,uint,4> uint4;

generic struct bgr { T b,g,r; };
typedef vec<bgr,uint8,3> byte3;
typedef vec<bgr,int32,3> bgr3i;
typedef vec<bgr,float,3> bgr3f;
generic struct rgb {
    T r,g,b;
    vec<::bgr,T,3> bgr() const { return vec<::bgr,T,3>{b,g,r}; }
};
typedef vec<rgb,float,3> rgb3f;

generic struct rgba {
    T r,g,b,a;
    //vec<rgb,T,3>& rgb() const { return *(vec< ::rgb,T,3>*)this; }
};
typedef vec<rgba,float,4> rgba4f;

generic struct bgra {
    T b,g,r,a;
    //vec<bgr,T,3>& bgr() const { return *(vec< ::bgr,T,3>*)this; }
    operator vec<rgb,T,3>() const { return vec<rgb,T,3>{r,g,b}; }
};
typedef vec<bgra,float,4> bgra4f;
struct byte4 : vec<bgra,uint8,4> {
    using vec::vec;
    byte4() : vec() {}
    byte4(vec<rgb,uint8,3> rgb) : vec(rgb.b, rgb.g, rgb.r, 0xFF) {}
};

template<template<Type> class V, Type T> inline vec<V,T,3> cross(vec<V,T,3> a, vec<V,T,3> b) { return vec<V,T,3>(a.y*b.z - b.y*a.z, a.z*b.x - b.z*a.x, a.x*b.y - b.x*a.y); }
template<template<Type> class V, Type T> inline vec<V,T,3> _cross(vec<V,T,3> a, vec<V,T,3> b) { return vec<V,T,3>(b.y*a.z - a.y*b.z, b.z*a.x - a.z*b.x, b.x*a.y - a.x*b.y); }

inline String strx(uint2 N) { return str(N.x)+'x'+str(N.y); }
