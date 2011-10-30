#pragma once
#include "core.h"

template <class V, class T, int N> struct vector : public V {
	vector(){}
	vector(T a, T b):V{a,b}{}
	vector(T a, T b, T c, T d):V{a,b,c,d}{}
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
template <class V, class T, int N> vector<V,T,N> abs(vector<V,T,N> v) { vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=abs(v[i]); return r; }

template<class T> struct xy { T x=0,y=0; };
typedef vector<xy<int>,int,2> int2;
typedef vector<xy<float>,float,2> vec2;
template<class T> struct ia { T i=0,a=0; };
typedef vector<ia<uint8>,uint8,2> byte2;
template<class T> struct rgba { T r=0,g=0,b=0,a=0; };
typedef vector<rgba<uint8>,uint8,4> byte4;
template<class T> struct xyzw { T x=0,y=0,z=0,w=0; };
typedef vector<xyzw<float>,float,2> vec4;
