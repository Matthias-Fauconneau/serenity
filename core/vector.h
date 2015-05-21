#pragma once
/// \file vector.h Vector definitions and operations
#include "string.h"
#include "math.h"

/// Provides vector operations on \a N packed values of type \a T stored in struct \a V<T>
/// \note statically inheriting the data type allows to provide vector operations to new types and to access named components directly
template<template<Type> /*Type*/class V, Type T, uint N> struct vec : V<T> {
    static_assert(sizeof(V<T>)==N*sizeof(T), ""/*req C++14*/);

    /// Defaults initializes to zero
	inline vec() : vec(0) {}
    /// Initializes all components to the same value \a v
    inline vec(T v){ for(uint i: range(N)) at(i)=v; }
    /// Initializes components separately
	template<Type... Args> inline explicit constexpr vec(T a, T b, Args... args) : V<T>{a,b,T(args)...} {
		static_assert(sizeof...(args) == N-2, "Invalid number of arguments");
    }
    /// Initializes components from a fixed size array
	template<Type... Args> inline explicit vec(const T o[N]){ for(uint i: range(N)) at(i)=(T)o[i]; }

    /// Initializes components from another vec \a o casting from \a S to \a T
	template<Type S> inline explicit vec(const vec<V,S,N>& o) { for(uint i: range(N)) at(i)=(T)o[i]; }

	/// Initializes first components from another vec \a o and initializes remaining components with args...
    template<template<Type> /*Type*/class W, Type... Args> vec(const vec<W,T,N-sizeof...(Args)>& o, Args... args){
		for(int i: range(N-sizeof...(Args))) at(i)=o[i];
		T unpacked[]={T(args)...}; for(int i: range(sizeof...(Args))) at(N-sizeof...(Args)+i)=unpacked[i];
	}

    operator ref<T>() const { return ref<T>((T*)this, N); }

    /// \name Accessors
	inline const T& at(uint i) const { return ((T*)this)[i]; }
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

#undef generic
#define generic template<template<Type> /*Type*/class V, Type T, uint N> inline /*constexpr*/
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
generic vec floor(const vec& v){ vec r; for(uint i: range(N)) r[i]=floor(v[i]); return r;  }
generic vec fract(const vec& v){ vec r; for(uint i: range(N)) r[i]=mod(v[i],1); return r;  }
generic vec round(const vec& v){ vec r; for(uint i: range(N)) r[i]=round(v[i]); return r;  }
generic vec ceil(const vec& v){ vec r; for(uint i: range(N)) r[i]=ceil(v[i]); return r;  }
generic vec min(const vec& a, const vec& b){ vec r; for(uint i: range(N)) r[i]=min(a[i],b[i]); return r; }
generic vec max(const vec& a, const vec& b){ vec r; for(uint i: range(N)) r[i]=max(a[i],b[i]); return r; }
generic vec clamp(const vec& min, const vec& x, const vec& max){vec r; for(uint i: range(N)) r[i]=clamp(min[i],x[i],max[i]); return r;}

generic T min(const vec& v) { return min((ref<T>)v); }
generic T sum(const vec& a) { T sum=0; for(uint i: range(N)) sum+=a[i]; return sum; }
generic T product(const vec& a) { T product=1; for(uint i: range(N)) product *= a[i]; return product; }
generic T dot(const vec& a, const vec& b) { T ssq=0; for(uint i: range(N)) ssq += a[i]*b[i]; return ssq; }
generic T sq(const vec& a) { return dot(a,a); }
//generic float length(const vec& a) { return sqrt(dot(a,a)); }
//generic vec normalize(const vec& a){ return a/length(a); }
generic bool isNaN(const vec& v){ for(uint i: range(N)) if(isNaN(v[i])) return true; return false; }
generic bool isNumber(const vec& v){ for(uint i: range(N)) if(!isNumber(v[i])) return false; return true; }

#undef generic
#define generic template<Type T>

template<template<Type> /*Type*/class V, Type T, uint N> inline String str(const vec& v) {
	array<char> s(6*N, 0); s.append('('); for(uint i: range(N)) { s.append(str(v[i])); if(i<N-1) s.append(", "); } s.append(')'); return move(s);
}

#undef vec

generic struct xy { T x,y; };
/// Integer x,y vector (8bit)
typedef vec<xy,uint8,2> byte2;
/// Integer x,y vector (16bit)
typedef vec<xy,int16,2> short2;
/// Integer x,y vector (32bit)
typedef vec<xy,int,2> int2;
inline String strx(int2 N) { return str(N.x)+'x'+str(N.y); }
/// Single precision x,y vector
typedef vec<xy,float,2> float2;
typedef vec<xy,float,2> vec2f;
typedef vec<xy,float,2> vec2;
inline String strx(vec2 N) { return str(N.x)+'x'+str(N.y); }

generic struct xyz {
    T x,y,z;
    vec<xy,T,2> xy() const { return vec< ::xy,T,2>(x,y); }
};
/// Integer x,y,z vector
typedef vec<xyz,int,3> int3;
/// Integer x,y,z vector (16bit)
typedef vec<xyz,uint16,3> short3;
/// Floating-point x,y,z vector
typedef vec<xyz,float,3> float3;
typedef vec<xyz,float,3> vec3f;
/// Double precision floating-point x,y,z vector
typedef vec<xyz,double,3> vec3d;
typedef vec<xyz,float,3> vec3;

inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y*b.z - b.y*a.z, a.z*b.x - b.z*a.x, a.x*b.y - b.x*a.y); }
/*inline vec3 normal(vec3 v) {
	int index=0; float min=v[0];
	for(int i: range(3)) if(abs(v[i]) < min) { index=i; min=abs(v[i]); }
	vec3 t=0; t[index]=1;
	return normalize(cross(v, t));
}*/

generic struct xyzw {
    T x,y,z,w;
    vec< ::xyz,T,3> xyz() const { return *(vec< ::xyz,T,3>*)this; }
    vec< ::xyz,T,3> xyw() const { return vec< ::xyz,T,3>{x,y,w}; }
    vec< ::xy,T,2> xy()const{ return *(vec< ::xyz,T,2>*)this; }
};
/// Floating-point x,y,z,w vector
typedef vec<xyzw,float,4> vec4f;
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
	//using vec::vec;
    byte4() : vec(0) {} // Defaults initalizes to zero
	inline byte4(byte v) : vec(v) {}
	inline byte4(byte b, byte g, byte r, byte a=0xFF) : vec(b,g,r,a) {}
	// bgr
	byte4(byte3 bgr, uint8 a = 0xFF) : vec(bgr.b, bgr.g, bgr.r, a) {}
	//byte3 bgr() { return byte3(b, g, r); } // -> bgra
	// bgr3f
	byte4(bgr3f bgr, uint8 a = 0xFF) : vec(bgr.b, bgr.g, bgr.r, a) {}
	bgr3f bgr() { return bgr3f(b, g, r); }
	// rgba
	byte4(vec<rgba,uint8,4> rgba) : vec(rgba.b, rgba.g, rgba.r, rgba.a) {}
	byte4(vec<rgb,uint8,3> rgb) : vec(rgb.b, rgb.g, rgb.r, 0xFF) {}
};
/// Integer b,g,r,a vector (32bit)
typedef vec<bgra,int,4> int4;
/// Unsigned integer b,g,r,a vector (32bit)
typedef vec<bgra,uint,4> uint4;

/// Integer x,y vector (64bit)
typedef vec<xy,int64,2> long2;

template<template<Type> /*Type*/class V, Type T, uint N> inline /*constexpr*/ float length(const vec<V,T,N>& a) { return sqrt(dot(a,a)); }

struct quat {
    float s = 1; vec3 v = 0;
    quat conjugate() const { return {s, -v}; }
};
inline quat operator*(quat p, quat q) { return {p.s*q.s - dot(p.v, q.v), p.s*q.v + q.s*p.v + cross(p.v, q.v)}; }
inline quat operator*(float s, quat q) { return {s*q.s, s*q.v}; }
inline quat operator+(quat p, quat q) { return {p.s+q.s, p.v+q.v}; }
inline quat normalize(quat q) { return 1./sqrt(sq(q.s)+sq(q.v)) * q; }
inline String str(quat q) { return "["+str(q.s, q.v)+"]"; }

template<Type A, Type B> struct pair { A a; B b; };
inline pair<vec3, vec3> closest(vec3 a1, vec3 a2, vec3 b1, vec3 b2) {
    const vec3 u = a2 - a1, v = b2 - b1, w = a1 - b1;
    const float  a = dot(u,u), b = dot(u,v), c = dot(v,v), d = dot(u,w), e = dot(v,w);
    const float D = a*c - b*b; float sD = D,  tD = D;
    // Compute the line parameters of the two closest points
    real sN, tN;
    if(D < __FLT_EPSILON__) sN = 0, sD = 1, tN = e, tD = c;
    else {
        sN = (b*e - c*d), tN = (a*e - b*d);
        /**/  if(sN < 0) { sN = 0, tN = e, tD = c; }
        else if (sN > sD) { sN = sD; tN = e + b; tD = c; }
    }
    /**/  if(tN < 0) {
        tN = 0;
        /**/  if(-d < 0) sN = 0;
        else if(-d > a) sN = sD;
        else { sN = -d; sD = a; }
    }
    else if(tN > tD) {
        tN = tD;
        /**/  if((-d + b) < 0) sN = 0;
        else if((-d + b) > a) sN = sD;
        else { sN = (-d + b); sD = a; }
    }
    float sc = abs(sN) < __FLT_EPSILON__ ? 0 : sN / sD;
    float tc = abs(tN) < __FLT_EPSILON__ ? 0 : tN / tD;
    return {a1 + (sc * u), b1 - (tc * v)};
}
