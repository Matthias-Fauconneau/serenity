#pragma once
/// \file vector.h Vector definitions and operations
#include "string.h"
#include "math.h"

/// Provides vector operations on \a N packed values of type \a T stored in struct \a V<T>
/// \note statically inheriting the data type allows to provide vector operations to new types and to access named components directly
template<template<typename> class V, Type T, uint N> struct vector : V<T> {
    static_assert(sizeof(V<T>)==N*sizeof(T),"");

    vector(){}
    /// Initializes all components to the same value \a v
    vector(T v){ for(uint i=0;i<N;i++) at(i)=v; }
    /// Initializes components separately
    template<Type... Args> explicit constexpr vector(T a, T b, Args... args):V<T>{a,b,T(args)...}{
        static_assert(sizeof...(args) == N-2, "Invalid number of arguments");
    }
    /// Initializes components from a fixed size array
    template<Type... Args> explicit vector(const T o[N]){ for(uint i=0;i<N;i++) at(i)=(T)o[i]; }
    /// Initializes first components from another vector \a o and initializes remaining components with args...
    template<template<typename> class W, Type... Args> vector(const vector<W,T,N-sizeof...(Args)>& o, Args... args){
        // Assumes the compiler will be able to remove this fluff
        T concatenated[N]; for(int i: range(N-sizeof...(Args))) concatenated[i]=o[i];
        T unpacked[]={args...}; for(int i: range(sizeof...(Args))) concatenated[N-sizeof...(Args)+i]=unpacked[i];
        *this = vector(concatenated);
    }
    /// Initializes components from another vector \a o casting from \a T2 to \a T
    template<template<typename> class W, Type F> explicit vector(const vector<W,F,N>& o) { for(uint i=0;i<N;i++) at(i)=(T)o[i]; }
    /// Unchecked accessor (const)
    const T& at(uint i) const { return ((T*)this)[i]; }
    /// Unchecked accessor
    T& at(uint i) { return ((T*)this)[i]; }
    /// Accessor (checked in debug build, const)
    const T& operator[](uint i) const { assert(i<N); return at(i); }
    /// Accessor (checked in debug build)
    T& operator[](uint i) { assert(i<N); return at(i); }
    /// \name Operators
    explicit operator bool() const { for(uint i=0;i<N;i++) if(at(i)!=0) return true; return false; }
    vector& operator +=(const vector& v) { for(uint i=0;i<N;i++) at(i)+=v[i]; return *this; }
    vector& operator -=(const vector& v) { for(uint i=0;i<N;i++) at(i)-=v[i]; return *this; }
    vector& operator *=(const vector& v) { for(uint i=0;i<N;i++) at(i)*=v[i]; return *this; }
    vector& operator *=(const T& s) { for(uint i=0;i<N;i++) at(i)*=s; return *this; }
    vector& operator /=(const T& s) { for(uint i=0;i<N;i++) at(i)/=s; return *this; }
    /// \}
};

#undef generic
#define generic template <template <typename> class V, Type T, uint N>
#define vector vector<V,T,N>

generic vector rotate(const vector& u) { vector r=u; for(uint i=0;i<N-1;i++) swap(r[i],r[i+1]); return r; }

generic vector operator +(const vector& u) { return u; }
generic vector operator -(const vector& u) { vector r; for(uint i=0;i<N;i++) r[i]=-u[i]; return r; }
generic vector operator +(const vector& u, const vector& v) { vector r; for(uint i=0;i<N;i++) r[i]=u[i]+v[i]; return r; }
generic vector operator -(const vector& u, const vector& v) { vector r; for(uint i=0;i<N;i++) r[i]=u[i]-v[i]; return r; }
generic vector operator *(const vector& u, const vector& v) { vector r; for(uint i=0;i<N;i++) r[i]=u[i]*v[i]; return r; }
generic vector operator *(const vector& u, T s) { vector r; for(uint i=0;i<N;i++) r[i]=u[i]*s; return r; }
generic vector operator *(T s, const vector& u) { vector r; for(uint i=0;i<N;i++) r[i]=s*u[i]; return r; }
generic vector operator /(T s, const vector& u) { vector r; for(uint i=0;i<N;i++) r[i]=s/u[i]; return r; }
generic vector operator /(const vector& u, T s) { vector r; for(uint i=0;i<N;i++) r[i]=u[i]/s; return r; }
generic vector operator /(const vector& u, const vector& v) { vector r; for(uint i=0;i<N;i++) r[i]=u[i]/v[i]; return r; }
generic bool operator <(const vector& u, const vector& v) { for(uint i=0;i<N;i++) if(u[i]>=v[i]) return false; return true; }
generic bool operator <=(const vector& u, const vector& v) { for(uint i=0;i<N;i++) if(u[i]>v[i]) return false; return true; }
generic bool operator ==(const vector& u, const vector& v) { for(uint i=0;i<N;i++) if(u[i]!=v[i]) return false; return true; }
generic bool operator >=(const vector& u, const vector& v) { for(uint i=0;i<N;i++) if(u[i]<v[i]) return false; return true; }
generic bool operator >(const vector& u, const vector& v) { for(uint i=0;i<N;i++) if(u[i]<=v[i]) return false; return true; }

generic vector abs(const vector& v){ vector r; for(uint i=0;i<N;i++) r[i]=abs(v[i]); return r;  }
generic T sign(T x) { return x > 0 ? 1 : x < 0 ? -1 : 0; }
generic vector sign(const vector& v){ vector r; for(uint i=0;i<N;i++) r[i]=ceil(v[i]); return r;  }
generic vector floor(const vector& v){ vector r; for(uint i=0;i<N;i++) r[i]=floor(v[i]); return r;  }
generic vector fract(const vector& v){ vector r; for(uint i=0;i<N;i++) r[i]=mod(v[i],1); return r;  }
generic vector round(const vector& v){ vector r; for(uint i=0;i<N;i++) r[i]=round(v[i]); return r;  }
generic vector ceil(const vector& v){ vector r; for(uint i=0;i<N;i++) r[i]=ceil(v[i]); return r;  }
generic vector min(const vector& a, const vector& b){ vector r; for(uint i=0;i<N;i++) r[i]=min(a[i],b[i]); return r; }
generic vector max(const vector& a, const vector& b){ vector r; for(uint i=0;i<N;i++) r[i]=max(a[i],b[i]); return r; }
generic vector clip(const vector& min, const vector& x, const vector& max){vector r; for(uint i=0;i<N;i++) r[i]=clip(min[i],x[i],max[i]); return r;}

generic float dot(const vector& a, const vector& b) { float l=0; for(uint i=0;i<N;i++) l+=a[i]*b[i]; return l; }
generic float sq(const vector& a) { return dot(a,a); }
generic float norm(const vector& a) { return sqrt(dot(a,a)); }
generic vector normalize(const vector& a){ return a/norm(a); }
generic bool isNaN(const vector& v){ for(uint i=0;i<N;i++) if(isNaN(v[i])) return true; return false; }

generic String str(const vector& v) { String s("("_); for(uint i=0;i<N;i++) { s<<str(v[i]); if(i<N-1) s<<", "_; } s<<")"_; return s; }

#undef vector
#undef generic
#define generic template<Type T>

generic struct xy { T x,y; };
/// Integer x,y vector
typedef vector<xy,int,2> int2;
/// Integer x,y vector (16bit)
typedef vector<xy,uint16,2> short2;
/// Single precision x,y vector
typedef vector<xy,float,2> vec2;
inline float cross(vec2 a, vec2 b) { return a.y*b.x - a.x*b.y; }
inline float cross(int2 a, int2 b) { return a.y*b.x - a.x*b.y; }
inline vec2 normal(vec2 a) { return vec2(-a.y, a.x); }

generic struct xyz {
    T x,y,z;
    vector<xy,T,2> xy() const { return vector< ::xy,T,2>(x,y); }
};
/// Integer x,y,z vector
typedef vector<xyz,int,3> int3;
/// Integer x,y,z vector (16bit)
typedef vector<xyz,uint16,3> short3;
/// Floating-point x,y,z vector
typedef vector<xyz,float,3> vec3;
inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y*b.z - b.y*a.z, a.z*b.x - b.z*a.x, a.x*b.y - b.x*a.y); }
inline vec3 normal(vec3 v) {
    int index=0; float min=v[0];
    for(int i: range(3)) if(abs(v[i]) < min) index=i, min=abs(v[i]);
    vec3 t=0; t[index]=1;
    return normalize(cross(v, t));
}

generic struct xyzw {
    T x,y,z,w;
    vector< ::xyz,T,3> xyz() const { return vector< ::xyz,T,3>(x,y,z); }
    vector< ::xyz,T,3> xyw() const { return vector< ::xyz,T,3>(x,y,w); }
    vector< ::xy,T,2> xy()const{ return vector< ::xy,T,2>(x,y); }
};
/// Floating-point x,y,z,w vector
typedef vector<xyzw,float,4> vec4;

generic struct bgra { T b,g,r,a; };
/// Integer b,g,r,a vector (8bit)
typedef vector<bgra,uint8,4> byte4;
