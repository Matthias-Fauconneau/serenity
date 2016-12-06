#pragma once
/// \file vector.h Vector definitions and operations
#include "string.h"
#include "math.h"

/// Provides vector operations on \a N packed values of type \a T stored in struct \a V<T>
/// \note statically inheriting the data type allows to provide vector operations to new types and to access named components directly
template<template<Type> /*Type*/class V, Type T, uint N> struct vec : V<T> {
 static_assert(sizeof(V<T>)==N*sizeof(T));
 typedef T _T;
 static constexpr uint _N = N;

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
generic vec fract(const vec& v){ vec r; for(uint i: range(N)) r[i]=fract(v[i]); return r;  }
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
generic float length(const vec& a) { return sqrt(sq(a)); }
generic vec normalize(const vec& a){ return a/length(a); }
generic bool isNaN(const vec& v){ for(uint i: range(N)) if(isNaN(v[i])) return true; return false; }
generic bool isNumber(const vec& v){ for(uint i: range(N)) if(!isNumber(v[i])) return false; return true; }

#undef generic
#define generic template<Type T>

template<template<Type> class V, Type T, uint N> inline String str(const vec& v) {
 buffer<char> s(16*N, 0);
 s.append('(');
 for(uint i: range(N)) { s.append(str(v[i],2u)); if(i<N-1) s.append(" "); }
 s.append(')');
 return move(s);
}

#undef vec

generic struct xy {
 T x,y;
 vec< ::xy, T, 2> yx() const { return vec< ::xy, T, 2>{y,x}; }
};
typedef vec<xy,uint8,2> byte2;
typedef vec<xy,int,2> int2;
typedef vec<xy,uint,2> uint2;
typedef vec<xy,float,2> vec2;

generic struct xyz {
 T x,y,z;
 vec<xy,T,2>& xy() const { return (vec< ::xy,T,2>&)*this; }
};
/// Integer x,y,z vector
typedef vec<xyz,int,3> int3;
typedef vec<xyz,uint,3> uint3;
typedef vec<xyz,float,3> vec3;

generic struct xyzw {
 T x,y,z,w;
 vec< ::xy,T,2> xy() const { return *(vec< ::xy,T,2>*)this; }
 vec< ::xyz,T,3> xyz() const { return *(vec< ::xyz,T,3>*)this; }
 vec< ::xyz,T,3> xyw() const { return vec3(x, y, w); }
};
typedef vec<xyzw,float,4> vec4;
typedef vec<xyzw,uint,4> uint4;

generic struct bgr { T b,g,r; };
typedef vec<bgr,uint8,3> byte3;
typedef vec<bgr,int,3> bgr3i;
typedef vec<bgr,float,3> bgr3f;
generic struct rgb { T r,g,b; };
typedef vec<rgb,float,3> rgb3f;

generic struct rgba { T r,g,b,a; };
generic struct bgra {
    T b,g,r,a;
    vec<bgr,T,3>& bgr() const { return *(vec< ::bgr,T,3>*)this; }
    operator vec<rgb,T,3>() const { return vec<rgb,T,3>{r,g,b}; }
};
typedef vec<bgra,float,4> bgra4f;
struct byte4 : vec<bgra,uint8,4> {
 using vec::vec;
 byte4() : vec() {}
 byte4(vec<rgb,uint8,3> rgb) : vec(rgb.b, rgb.g, rgb.r, 0xFF) {}
};

template<template<Type> class V, Type T> inline vec<V,T,3> cross(vec<V,T,3> a, vec<V,T,3> b) { return vec<V,T,3>(a.y*b.z - b.y*a.z, a.z*b.x - b.z*a.x, a.x*b.y - b.x*a.y); }

inline String strx(uint2 N) { return str(N.x)+'x'+str(N.y); }
