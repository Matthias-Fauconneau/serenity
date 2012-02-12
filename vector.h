#pragma once
#include "core.h"
#include "math.h"
#define PI M_PI

//TODO: SIMD
template <template <typename> class V, class T, int N> struct vector : V<T> {
    static const int size = N;
    vector():V<T>{}{}
    template<class... Args> explicit vector(Args... args):V<T>{args...}{static_assert(sizeof...(args) == N, "Invalid number of arguments");}
    template<class T2> explicit vector(const vector<V,T2,N>& o) { for(int i=0;i<N;i++) u(i)=(T)o[i]; }
    const T& u(int i) const { return ((T*)this)[i]; }
    T& u(int i) { return ((T*)this)[i]; }
    const T& operator[](int i) const { assert(i>=0 && i<N); return u(i); }
    T& operator[](int i) { assert(i>=0 && i<N); return u(i); }
    vector operator +=(const vector& v) { for(int i=0;i<N;i++) u(i)+=v[i]; return *this; }
    vector operator -=(const vector& v) { for(int i=0;i<N;i++) u(i)-=v[i]; return *this; }
    vector operator *=(const vector& v) { for(int i=0;i<N;i++) u(i)*=v[i]; return *this; }
    vector operator *=(float s) { for(int i=0;i<N;i++) u(i)*=s; return *this; }
    vector operator /=(const vector& v) { for(int i=0;i<N;i++) u(i)/=v[i]; return *this; }
    vector operator -() const { vector r; for(int i=0;i<N;i++) r[i]=-u(i); return r; }
    vector operator +(vector v) const { vector r=*this; r+=v; return r; }
    vector operator -(vector v) const { vector r=*this; r-=v; return r; }
    vector operator *(vector v) const { vector r=*this; r*=v; return r; }
    vector operator /(vector v) const { vector r=*this; r/=v; return r; }
    vector operator *(float s) const { vector r=*this; r*=s; return r; }
    vector operator /(float s) const { return *this*(1/s); }
    bool operator ==(const vector& v) const { for(int i=0;i<N;i++) if(u(i)!=v[i]) return false; return true; }
    bool operator !=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)!=v[i]) return true; return false; }
    bool operator >(const vector& v) const { for(int i=0;i<N;i++) if(u(i)<=v[i]) return false; return true; }
    bool operator <(const vector& v) const { for(int i=0;i<N;i++) if(u(i)>=v[i]) return false; return true; }
    bool operator >=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)<v[i]) return false; return true; }
    bool operator <=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)>v[i]) return false; return true; }
    explicit operator bool() const { for(int i=0;i<N;i++) if(u(i)!=0) return true; return false; }
};

#define generic template <template <typename> class V, class T, int N>
generic vector<V,T,N> operator *(float s, vector<V,T,N> v){ return v*s; }
generic vector<V,T,N> abs(const vector<V,T,N>& v){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=abs(v[i]); return r;  }
generic vector<V,T,N> sign(const vector<V,T,N>& v){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=sign(v[i]); return r;  }
generic vector<V,T,N> min(const vector<V,T,N>& a, const vector<V,T,N>& b){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=min(a[i],b[i]); return r;  }
generic vector<V,T,N> max(const vector<V,T,N>& a, const vector<V,T,N>& b){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=max(a[i],b[i]); return r;  }
generic vector<V,T,N> mix(const vector<V,T,N>& a,const vector<V,T,N>& b, float t) { return a*t + b*(1-t); }
generic float length(const vector<V,T,N>& a) { float l=0; for(int i=0;i<N;i++) l+=a[i]*a[i]; return sqrt(l); }
generic vector<V,T,N> normalize(const vector<V,T,N>& a){ return a/length(a); }
generic void log_(const vector<V,T,N>& v) {
    log_('(');
    for(int i=0;i<N;i++) { log_(v[i]); if(i<N-1) log_(", "_); }
    log_(')');
}
#undef generic

template<class T> struct xy { T x,y; };
typedef vector<xy,int,2> int2;
typedef vector<xy,float,2> vec2;

template<class T> struct xyz { T x,y,z; vector<xy,T,2> xy()const{return vector< ::xy,T,2>{x,y};} };
typedef vector<xyz,float,3> vec3;
typedef vector<xyz,int,3> int3;

template<class T> struct xyzw { T x,y,z,w; vec2 xy()const{return vec2{x,y};} };
typedef vector<xyzw,float,4> vec4;

template<class T> struct bgra { T b,g,r,a; };
typedef vector<bgra,uint8,4> byte4;
typedef vector<bgra,int,4> int4;

template<class T> struct rgba { T r,g,b,a; operator byte4()const{return byte4{b,g,r,a};} };
typedef vector<rgba,uint8,4> rgba4;
template<class T> struct ia { T i,a; operator byte4()const{return byte4{i,i,i,a};}};
typedef vector<ia,uint8,2> byte2;

inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x); }
