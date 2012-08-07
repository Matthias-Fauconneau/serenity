#pragma once
#include "string.h"

extern struct Zero {} zero; //dummy type to call zero-initializing constructor //TODO: fix missing initializers
template<template<typename> class V, class T, int N> struct vector : V<T> {
    static const int size = N;
    vector():i(V<T>{}){}
    vector(Zero):i(V<T>{}){}
    template<class... Args> explicit vector(T x, T y, Args... args):i(V<T>{x,y,T(args)...}){
        static_assert(sizeof...(args) == N-2, "Invalid number of arguments");
    }
    template<class T2> explicit vector(const vector<V,T2,N>& o) { for(int i=0;i<N;i++) u(i)=(T)o[i]; }
    const T& u(int i) const { return ((T*)this)[i]; }
    T& u(int i) { return ((T*)this)[i]; }
    const T& operator[](uint i) const { assert_(i<N); return u(i); }
    T& operator[](uint i) { assert_(i<N); return u(i); }
    vector& operator +=(const vector& v) { for(int i=0;i<N;i++) u(i)+=v[i]; return *this; }
    vector& operator -=(const vector& v) { for(int i=0;i<N;i++) u(i)-=v[i]; return *this; }
    vector& operator *=(const vector& v) { for(int i=0;i<N;i++) u(i)*=v[i]; return *this; }
    vector operator -() const { vector r; for(int i=0;i<N;i++) r[i]=-u(i); return r; }
    explicit operator bool() const { for(int i=0;i<N;i++) if(u(i)!=0) return true; return false; }
};

#define generic template <template <typename> class V, class T, int N>
#define vector vector<V,T,N>

generic vector operator +(const vector& u, const vector& v) { vector r; for(int i=0;i<N;i++) r[i]=u[i]+v[i]; return r; }
generic vector operator -(const vector& u, const vector& v) { vector r; for(int i=0;i<N;i++) r[i]=u[i]-v[i]; return r; }
generic vector operator *(const vector& u, const vector& v) { vector r; for(int i=0;i<N;i++) r[i]=u[i]*v[i]; return r; }
generic vector operator *(const vector& u, int s) { vector r; for(int i=0;i<N;i++) r[i]=u[i]*s; return r; }
generic vector operator *(int s, vector v){ return v*s; }
generic vector operator /(const vector& u, uint s) { vector r; for(int i=0;i<N;i++) r[i]=u[i]/s; return r; }
generic bool operator ==(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]!=v[i]) return false; return true; }
//generic bool operator !=(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]!=v[i]) return true; return false; }
generic bool operator >(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]<=v[i]) return false; return true; }
generic bool operator <(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]>=v[i]) return false; return true; }
generic bool operator >=(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]<v[i]) return false; return true; }
generic bool operator <=(const vector& u, const vector& v) { for(int i=0;i<N;i++) if(u[i]>v[i]) return false; return true; }

generic vector abs(vector v){ vector r; for(int i=0;i<N;i++) r[i]=abs(v[i]); return r;  }
generic vector min(vector a, vector b){ vector r; for(int i=0;i<N;i++) r[i]=min(a[i],b[i]); return r;  }
generic vector max(vector a, vector b){ vector r; for(int i=0;i<N;i++) r[i]=max(a[i],b[i]); return r;  }
generic vector clip(T min, vector x, T max){ vector r; for(int i=0;i<N;i++) r[i]=clip(min,x[i],max); return r;  }

generic string str(vector v) {
    string s = string("("_);
    for(int i=0;i<N;i++) { s<<str(v[i]); if(i<N-1) s<<", "_; }
    return s+")"_;
}
#undef vector
#undef generic

template<class T> struct xy { T x,y; };
typedef vector<xy,int,2> int2;
