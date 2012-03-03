#pragma once
#include "string.h"
#include "math.h"
#define PI M_PI

extern struct Zero {} zero; //dummy type to call zero-initializing constructor
//TODO: SIMD
template <template <typename> class V, class T, int N> struct vector : V<T> {
    static const int size = N;
    vector(){}
    vector(Zero):V<T>{}{}
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
    //vector operator /=(const vector& v) { for(int i=0;i<N;i++) u(i)/=v[i]; return *this; }
    vector operator -() const { vector r; for(int i=0;i<N;i++) r[i]=-u(i); return r; }
    vector operator +(vector v) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)+v[i]; return r; }
    vector operator -(vector v) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)-v[i]; return r; }
    vector operator *(vector v) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)*v[i]; return r; }
    //vector operator /(vector v) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)/v[i]; return r; }
    vector operator *(float s) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)*s; return r; }
    vector operator /(float s) const { vector r; for(int i=0;i<N;i++) r[i]=u(i)/s; return r; }
    bool operator ==(const vector& v) const { for(int i=0;i<N;i++) if(u(i)!=v[i]) return false; return true; }
    bool operator !=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)!=v[i]) return true; return false; }
    bool operator >(const vector& v) const { for(int i=0;i<N;i++) if(u(i)<=v[i]) return false; return true; }
    bool operator <(const vector& v) const { for(int i=0;i<N;i++) if(u(i)>=v[i]) return false; return true; }
    bool operator >=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)<v[i]) return false; return true; }
    bool operator <=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)>v[i]) return false; return true; }
    explicit operator bool() const { for(int i=0;i<N;i++) if(u(i)!=0) return true; return false; }
};

#define generic template <template <typename> class V, class T, int N>
generic vector<V,T,N> operator *(float s, const vector<V,T,N>& v){ return v*s; }
//generic vector<V,T,N> operator +(float s, const vector<V,T,N>& v){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=s+v[i]; return r; }
//generic vector<V,T,N> operator -(float s, const vector<V,T,N>& v){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=s-v[i]; return r; }
generic vector<V,T,N> abs(const vector<V,T,N>& v){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=abs(v[i]); return r;  }
generic vector<V,T,N> sign(const vector<V,T,N>& v){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=sign(v[i]); return r;  }
generic vector<V,T,N> min(const vector<V,T,N>& a, const vector<V,T,N>& b){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=min(a[i],b[i]); return r;  }
generic vector<V,T,N> max(const vector<V,T,N>& a, const vector<V,T,N>& b){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=max(a[i],b[i]); return r;  }
generic vector<V,T,N> clip(T min, const vector<V,T,N>& x, T max){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=clip(min,x[i],max); return r;  }
generic float dot(const vector<V,T,N>& a, const vector<V,T,N>& b) { float l=0; for(int i=0;i<N;i++) l+=a[i]*b[i]; return l; }
generic float length(const vector<V,T,N>& a) { return sqrt(dot(a,a)); }
generic vector<V,T,N> normalize(const vector<V,T,N>& a){ return a/length(a); }
generic string str(const vector<V,T,N>& v) {
    string s="("_;
    for(int i=0;i<N;i++) { s<<str(v[i]); if(i<N-1) s<<", "_; }
    return s+")"_;
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
typedef vector<bgra,float,4> float4;

template<class T> struct rgba { T r,g,b,a; operator byte4()const{return byte4{b,g,r,a};} };
typedef vector<rgba,uint8,4> rgba4;
template<class T> struct rgb { T r,g,b; operator byte4()const{return byte4{b,g,r,255};} };
typedef vector<rgb,uint8,3> rgb3;
template<class T> struct ia { T i,a; operator byte4()const{return byte4{i,i,i,a};}};
typedef vector<ia,uint8,2> byte2;

inline float cross(vec2 a, vec2 b) { return a.y*b.x - a.x*b.y; }
inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x); }

template<class T> T mix(const T& a,const T& b, float t) { return a*t + b*(1-t); }
template<class T> T sq(const T& x) { return x*x; }
template<class T> T cb(const T& x) { return x*x*x; }
