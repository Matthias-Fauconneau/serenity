#pragma once
/// \file vector.h Vector definitions and operations
#include "string.h"
#include "math.h" //round

/// Provides vector operations on \a N packed values of type \a T stored in struct \a V<T>
/// \note statically inheriting the data type allows to provide vector operations to new types and to access named components directly
template<template<typename> class V, Type T, uint N> struct vec : V<T> {
	static_assert(sizeof(V<T>)==N*sizeof(T),"");

    /// Defaults initializes to zero
    vec() : vec(0) {}
    /// Initializes all components to the same value \a v
    vec(T v){ for(uint i: range(N)) at(i)=v; }
    /// Initializes components separately
	template<Type... Args> explicit constexpr vec(T a, T b, Args... args) : V<T>{a,b,T(args)...}{
        static_assert(sizeof...(args) == N-2, "Invalid number of arguments");
    }
    /// Initializes components from a fixed size array
    template<Type... Args> explicit vec(const T o[N]){ for(uint i: range(N)) at(i)=(T)o[i]; }

    /// Initializes components from another vec \a o casting from \a S to \a T
    template<Type S> explicit vec(const vec<V,S,N>& o) { for(uint i: range(N)) at(i)=(T)o[i]; }
    /// Unchecked accessor (const)
    const T& at(uint i) const { return ((T*)this)[i]; }
    /// Unchecked accessor
    T& at(uint i) { return ((T*)this)[i]; }
    /// Accessor (checked in debug build, const)
    const T& operator[](uint i) const { assert(i<N); return at(i); }
    /// Accessor (checked in debug build)
    T& operator[](uint i) { assert(i<N); return at(i); }
    /// \name Operators
    explicit operator bool() const { for(uint i: range(N)) if(at(i)!=0) return true; return false; }
    vec& operator +=(const vec& v) { for(uint i: range(N)) at(i)+=v[i]; return *this; }
    vec& operator -=(const vec& v) { for(uint i: range(N)) at(i)-=v[i]; return *this; }
    vec& operator *=(const vec& v) { for(uint i: range(N)) at(i)*=v[i]; return *this; }
    vec& operator *=(const T& s) { for(uint i: range(N)) at(i)*=s; return *this; }
    vec& operator /=(const T& s) { for(uint i: range(N)) at(i)/=s; return *this; }
    /// \}
};

#undef generic
#define generic template<template<typename> class V, Type T, uint N>
#define vec vec<V,T,N>

generic vec rotate(const vec& u) { vec r=u; for(uint i=0;i<N-1;i++) swap(r[i],r[i+1]); return r; }

generic vec operator +(const vec& u) { return u; }
generic vec operator -(const vec& u) { vec r; for(uint i: range(N)) r[i]=-u[i]; return r; }
generic vec operator +(const vec& u, const vec& v) { vec r; for(uint i: range(N)) r[i]=u[i]+v[i]; return r; }
generic vec operator -(const vec& u, const vec& v) { vec r; for(uint i: range(N)) r[i]=u[i]-v[i]; return r; }
generic vec operator *(const vec& u, const vec& v) { vec r; for(uint i: range(N)) r[i]=u[i]*v[i]; return r; }
generic vec operator *(const vec& u, T s) { vec r; for(uint i: range(N)) r[i]=u[i]*s; return r; }
generic vec operator *(T s, const vec& u) { vec r; for(uint i: range(N)) r[i]=s*u[i]; return r; }
generic vec operator /(T s, const vec& u) { vec r; for(uint i: range(N)) r[i]=s/u[i]; return r; }
generic vec operator /(const vec& u, T s) { vec r; for(uint i: range(N)) r[i]=u[i]/s; return r; }
generic vec operator /(const vec& u, const vec& v) { vec r; for(uint i: range(N)) r[i]=u[i]/v[i]; return r; }
generic bool operator <(const vec& u, const vec& v) { for(uint i: range(N)) if(u[i]>=v[i]) return false; return true; }
generic bool operator <=(const vec& u, const vec& v) { for(uint i: range(N)) if(u[i]>v[i]) return false; return true; }
generic bool operator ==(const vec& u, const vec& v) { for(uint i: range(N)) if(u[i]!=v[i]) return false; return true; }
generic bool operator >=(const vec& u, const vec& v) { for(uint i: range(N)) if(u[i]<v[i]) return false; return true; }
generic bool operator >(const vec& u, const vec& v) { for(uint i: range(N)) if(u[i]<=v[i]) return false; return true; }

generic vec abs(const vec& v){ vec r; for(uint i: range(N)) r[i]=abs(v[i]); return r;  }
generic vec sign(const vec& v){ vec r; for(uint i: range(N)) r[i]=ceil(v[i]); return r;  }
generic vec floor(const vec& v){ vec r; for(uint i: range(N)) r[i]=floor(v[i]); return r;  }
generic vec fract(const vec& v){ vec r; for(uint i: range(N)) r[i]=mod(v[i],1); return r;  }
generic vec round(const vec& v){ vec r; for(uint i: range(N)) r[i]=round(v[i]); return r;  }
generic vec ceil(const vec& v){ vec r; for(uint i: range(N)) r[i]=ceil(v[i]); return r;  }
generic vec min(const vec& a, const vec& b){ vec r; for(uint i: range(N)) r[i]=min(a[i],b[i]); return r; }
generic vec max(const vec& a, const vec& b){ vec r; for(uint i: range(N)) r[i]=max(a[i],b[i]); return r; }
generic vec clip(const vec& min, const vec& x, const vec& max){vec r; for(uint i: range(N)) r[i]=clip(min[i],x[i],max[i]); return r;}

generic T sum(const vec& a) { T sum=0; for(uint i: range(N)) sum+=a[i]; return sum; }
generic T product(const vec& a) { T product=1; for(uint i: range(N)) product *= a[i]; return product; }
generic T dot(const vec& a, const vec& b) { T ssq=0; for(uint i: range(N)) ssq += a[i]*b[i]; return ssq; }
generic T sq(const vec& a) { return dot(a,a); }
generic float norm(const vec& a) { return sqrt(dot(a,a)); }
generic vec normalize(const vec& a){ return a/norm(a); }
generic bool isNaN(const vec& v){ for(uint i: range(N)) if(isNaN(v[i])) return true; return false; }
generic bool isNumber(const vec& v){ for(uint i: range(N)) if(!isNumber(v[i])) return false; return true; }

generic String str(const vec& v) {
	array<char> s(6*N, 0); s.append('('); for(uint i: range(N)) { s.append(str(v[i])); if(i<N-1) s.append(", "); } s.append(')'); return move(s);
}

#undef vec
#undef generic
#define generic template<Type T>

generic struct xy { T x,y; };
/// Integer x,y vector (16bit)
typedef vec<xy,int16,2> short2;
/// Integer x,y vector (32bit)
typedef vec<xy,int,2> int2;
inline String strx(int2 N) { return str(N.x)+'x'+str(N.y); }
/// Single precision x,y vector
typedef vec<xy,float,2> float2;
typedef vec<xy,float,2> vec2;

generic struct xyz {
    T x,y,z;
    vec<xy,T,2> xy() const { return vec< ::xy,T,2>(x,y); }
};
/// Integer x,y,z vector
typedef vec<xyz,int,3> int3;
/// Integer x,y,z vector (16bit)
typedef vec<xyz,uint16,3> short3;
/// Floating-point x,y,z vector
typedef vec<xyz,float,3> vec3;

generic struct xyzw {
    T x,y,z,w;
    vec< ::xyz,T,3> xyz() const { return *(vec< ::xyz,T,3>*)this; }
    vec< ::xyz,T,3> xyw() const { return vec< ::xyz,T,3>{x,y,w}; }
    vec< ::xy,T,2> xy()const{ return *(vec< ::xyz,T,2>*)this; }
};
/// Floating-point x,y,z,w vector
typedef vec<xyzw,float,4> vec4;

generic struct bgr {
    T b,g,r;
};
/// Integer b,g,r vector (8bit)
typedef vec<bgr,uint8,3> byte3;
/// Integer b,g,r vector (32bit)
typedef vec<bgr,int,3> bgr3i;
/// Floating-point b,g,r vector
typedef vec<bgr,float,3> bgr3f;

generic struct bgra;
generic struct rgb {
    T r,g,b;
};

generic struct rgba;
generic struct bgra {
    T b,g,r,a;
    vec<bgr,T,3>& bgr() const { return *(vec<::bgr,T,3>*)this; }
    operator vec<rgb,T,3>() const { return vec<rgb,T,3>{r,g,b}; }
    operator vec<rgba,T,4>() const { return vec<rgba,T,4>{r,g,b,a}; }
};

generic struct rgba {
    T r,g,b,a;
};

/// Integer b,g,r,a vector (8bit)
struct byte4 : vec<bgra,uint8,4> {
    using vec::vec;
    byte4() : vec() {} // Defaults initalizes to zero
    byte4(vec<rgba,uint8,4> rgba) : vec(rgba.b, rgba.g, rgba.r, rgba.a) {}
    byte4(vec<rgb,uint8,3> rgb) : vec(rgb.b, rgb.g, rgb.r, 0xFF) {}
    byte4(byte3 bgr) : vec(bgr.b, bgr.g, bgr.r, 0xFF) {}
};
/// Integer b,g,r,a vector (32bit)
typedef vec<bgra,int,4> int4;
/// Unsigned integer b,g,r,a vector (32bit)
typedef vec<bgra,uint,4> uint4;
