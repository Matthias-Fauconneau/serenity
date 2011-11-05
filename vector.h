#pragma once
#include "core.h"

//TODO: SIMD
template <class V, class T, int N> struct vector : V {
	static const int size = N;
	vector(){}
	vector(T a, T b):V(a,b){} //vector(T a, T b):V{a,b}{}
	vector(T a, T b, T c, T d):V(a,b,c,d){} //vector(T a, T b, T c, T d):V{a,b,c,d}{}
	const T& u(int i) const { return ((T*)this)[i]; }
	T& u(int i) { return ((T*)this)[i]; }
	template<class U,class R> explicit vector(const vector<U,R,N>& o) { for(int i=0;i<N;i++) u(i)=(T)o[i]; }
	const T& operator[](int i) const { assert(i>=0 && i<N); return u(i); }
	T& operator[](int i) { assert(i>=0 && i<N); return u(i); }
	inline vector operator +=(const vector& v) { for(int i=0;i<N;i++) u(i)+=v[i]; return *this; }
	inline vector operator -=(const vector& v) { for(int i=0;i<N;i++) u(i)-=v[i]; return *this; }
	inline vector operator *=(const vector& v) { for(int i=0;i<N;i++) u(i)*=v[i]; return *this; }
	inline vector operator *=(float s) { for(int i=0;i<N;i++) u(i)*=s; return *this; }
	inline vector operator /=(const vector& v) { for(int i=0;i<N;i++) u(i)/=v[i]; return *this; }
	inline vector operator +(vector v) const { vector r=*this; r+=v; return r; }
	inline vector operator -(vector v) const { vector r=*this; r-=v; return r; }
	inline vector operator *(vector v) const { vector r=*this; r*=v; return r; }
	inline vector operator /(vector v) const { vector r=*this; r/=v; return r; }
	inline vector operator *(float s) const { vector r=*this; r*=s; return r; }
	inline vector operator /(float d) const { float s=1/d; return *this*s; }
	inline bool operator ==(const vector& v) const { for(int i=0;i<N;i++) if(u(i)!=v[i]) return false; return true; }
	inline bool operator !=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)!=v[i]) return true; return false; }
	inline bool operator >(const vector& v) const { for(int i=0;i<N;i++) if(u(i)<=v[i]) return false; return true; }
	inline bool operator <(const vector& v) const { for(int i=0;i<N;i++) if(u(i)>=v[i]) return false; return true; }
	inline bool operator >=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)<v[i]) return false; return true; }
	inline bool operator <=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)>v[i]) return false; return true; }
};
template <class V, class T, int N> vector<V,T,N> operator *(float s, vector<V,T,N> v){ return v*s; }

#define map(map) \
template <class V, class T, int N> vector<V,T,N> map(const vector<V,T,N>& v){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=map(v[i]); return r; }
map(abs) map(min) map(max)
#undef map

template <class V, class T, int N> vector<V,T,N> mix(const vector<V,T,N>& a,const vector<V,T,N>& b, float t){ return a*t + b*(1-t); }

template <class V, class T, int N> inline void log_(const vector<V,T,N>& v) {
	log_('(');
	for(int i=0;i<N;i++) { log_(v[i]); if(i<N-1) log_(", "); }
	log_(')');
}

template<class T> struct xy { T x=0,y=0; xy(){} xy(T x, T y):x(x),y(y){} };
typedef vector<xy<int>,int,2> int2;
typedef vector<xy<float>,float,2> vec2;
template<class T> struct ia { T i=0,a=0; };
typedef vector<ia<uint8>,uint8,2> byte2;
template<class T> struct rgba { T r=0,g=0,b=0,a=0; rgba(){} rgba(T r, T g, T b, T a):r(r),g(g),b(b),a(a){} };
typedef vector<rgba<uint8>,uint8,4> byte4;
template<class T> struct xyzw { T x=0,y=0,z=0,w=0; xyzw(){} xyzw(T x, T y, T z, T w):x(x),y(y),z(z),w(w){} };
typedef vector<xyzw<float>,float,2> vec4;

inline float cross(vec2 a, vec2 b) { return a.x*b.y - a.y*b.x; }

struct mat32 {
	void set(float m11_, float m12_, float m21_, float m22_, float dx_, float dy_) { m11=m11_;m12=m12_;m21=m21_;m22=m22_;dx=dx_;dy=dy_; }
	mat32(float m11, float m12, float m21, float m22, float dx, float dy) { set(m11,m12,m21,m22,dx,dy); }
	mat32(float dx, float dy) { set(1,0,0,1,dx,dy); }
	mat32(vec2 t) { set(1,0,0,1,t.x,t.y); }
	mat32() { set(1,0,0,1,0,0); }
	inline mat32 operator*(mat32 m) const { return mat32( m11*m.m11 + m12*m.m21, m11*m.m12 + m12*m.m22,
														  m21*m.m11 + m22*m.m21, m21*m.m12 + m22*m.m22,
														  dx*m.m11  + dy*m.m21 + m.dx, dx*m.m12  + dy*m.m22 + m.dy ); }
	inline vec2 operator*(vec2 v) const { return vec2( m11*v.x + m21*v.y + dx, m12*v.x + m22*v.y + dy ); }
	float m11, m12, m21, m22, dx, dy;
};
