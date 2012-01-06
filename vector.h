#pragma once
#include "core.h"
#include "math.h"
#define PI M_PI
//TODO: SIMD
template <class V, class T, int N> struct vector : V {
	static const int size = N;
	vector(){}
	vector(T a, T b):V(a,b){} //vector(T a, T b):V{a,b}{}
	vector(T a, T b, T c):V(a,b,c){}
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
	inline vector operator -() const { vector r; for(int i=0;i<N;i++) r[i]=-u(i); return r; }
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
	/*inline bool operator >=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)<v[i]) return false; return true; }
	inline bool operator <=(const vector& v) const { for(int i=0;i<N;i++) if(u(i)>v[i]) return false; return true; }*/
	inline explicit operator bool() const { for(int i=0;i<N;i++) if(u(i)!=0) return true; return false; }
};
template <class V, class T, int N> vector<V,T,N> operator *(float s, vector<V,T,N> v){ return v*s; }

#define map(map) \
template <class V, class T, int N> vector<V,T,N> map(const vector<V,T,N>& v){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=map(v[i]); return r; }
map(abs) map(min) map(max)
#undef map

#define map(map) \
template <class V, class T, int N> vector<V,T,N> map(const vector<V,T,N>& a, const vector<V,T,N>& b){ vector<V,T,N> r; for(int i=0;i<N;i++) r[i]=map(a[i],b[i]); return r; }
map(min) map(max)
#undef map

template <class V, class T, int N> vector<V,T,N> mix(const vector<V,T,N>& a,const vector<V,T,N>& b, float t){ return a*t + b*(1-t); }

template <class V, class T, int N> vector<V,T,N> normalize(const vector<V,T,N>& a){ float l=0; for(int i=0;i<N;i++) l+=sqr(a[i]); return a*(1/sqrt(l)); }

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
template<class T> struct xyz { T x=0,y=0,z=0; xyz(){} xyz(T x, T y, T z):x(x),y(y),z(z){} };
typedef vector<xyz<float>,float,3> vec3;
template<class T> struct rgba { T r=0,g=0,b=0,a=0; rgba(){} rgba(T r, T g, T b, T a):r(r),g(g),b(b),a(a){} };
typedef vector<rgba<uint8>,uint8,4> byte4;
template<class T> struct xyzw { T x=0,y=0,z=0,w=0; xyzw(){} xyzw(T x, T y, T z, T w):x(x),y(y),z(z),w(w){} };
typedef vector<xyzw<float>,float,2> vec4;

inline float cross(vec2 a, vec2 b) { return a.x*b.y - a.y*b.x; }

inline float dot( vec3 a, vec3 b ) { return a.x*b.x + a.y*b.y + a.z*b.z; }
inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x); }
inline vec3 planeNormal(vec3 a, vec3 b, vec3 c) { return cross(c-a,c-b); }

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

struct mat3 {
	float data[3*3];
	inline float& m(int i, int j) { return data[j*3+i]; }
	inline float& operator()(int i, int j) { return m(i,j); }
	inline vec3 operator*(vec3 v) { vec3 r; for(int i=0;i<3;i++) r[i] = v.x*m(i,0)+v.y*m(i,1)+v.z*m(i,2); return r; }
};
struct mat4 {
	float data[4*4];
	inline mat4(int d=1) { for(int i=0;i<16;i++) data[i]=0; if(d!=0) for(int i=0;i<4;i++) m(i,i)=d; }
	inline float m(int i, int j) const { return data[j*4+i]; }
	inline float& m(int i, int j) { return data[j*4+i]; }
	inline float operator()(int i, int j) const { return m(i,j); }
	inline float& operator()(int i, int j) { return m(i,j); }
	inline vec4 operator*(vec4 v) const { vec4 r; for(int i=0;i<4;i++) r[i] = v.x*m(i,0)+v.y*m(i,1)+v.z*m(i,2)+v.w*m(i,3); return r; }
	inline vec3 operator*(vec3 v) const { vec4 r=*this*vec4(v.x,v.y,v.z,1.0); return vec3(r.x,r.y,r.z)/r.w; }
	inline mat4 operator*(mat4 b) const {
		mat4 r(0); for(int j=0;j<4;j++) for(int i=0;i<4;i++) for(int k=0;k<4;k++) r.m(i,j) += m(i,k)*b.m(k,j); return r;
	}
	inline void translate(vec3 v) { for(int i=0;i<4;i++) m(i,3) += m(i,0)*v.x + m(i,1)*v.y + m(i,2)*v.z; }
	inline void scale(float f) { for(int j=0;j<3;j++) for(int i=0;i<4;i++) m(i,j)*=f; }
	inline void scale(vec3 v) { for(int j=0;j<3;j++) for(int i=0;i<4;i++) m(i,j)*=v[j]; }
	inline void rotateX( float angle ) {
		float c=cos(angle),s=sin(angle); mat4 r; r.m(1,1) = c; r.m(2,2) = c; r.m(1,2) = -s; r.m(2,1) = s; *this = *this * r;
	}
	inline void rotateY( float angle ) {
		float c=cos(angle),s=sin(angle); mat4 r; r.m(0,0) = c; r.m(2,2) = c; r.m(2,0) = -s; r.m(0,2) = s; *this = *this * r;
	}
	inline void rotateZ( float angle ) {
		float c=cos(angle),s=sin(angle); mat4 r; r.m(0,0) = c; r.m(1,1) = c; r.m(0,1) = -s; r.m(1,0) = s; *this = *this * r;
	}
	inline void reflect( vec3 n, float d ) {
		mat4 r;
		r.m(0,0) = 1-2*n.x*n.x; r.m(0,1) =  -2*n.x*n.y; r.m(0,2) =  -2*n.x*n.z; r.m(0,3) = 2*d*n.x;
		r.m(1,0) =  -2*n.y*n.x; r.m(1,1) = 1-2*n.y*n.y; r.m(1,2) =  -2*n.y*n.z; r.m(1,3) = 2*d*n.y;
		r.m(2,0) =  -2*n.z*n.x; r.m(2,1) =  -2*n.z*n.y; r.m(2,2) = 1-2*n.z*n.z; r.m(2,3) = 2*d*n.z;
		*this = *this * r;
	}
	inline float det3(int j0, int j1, int j2, int i0, int i1, int i2) const {
		return  m(i0,j0) * (m(i1,j1) * m(i2,j2) - m(i2,j1) * m(i1,j2)) -
				m(i0,j1) * (m(i1,j0) * m(i2,j2) - m(i2,j0) * m(i1,j2)) +
				m(i0,j2) * (m(i1,j0) * m(i2,j1) - m(i2,j0) * m(i1,j1));
	}
	inline mat3 normalMatrix() const {
		float det = 1 / det3(0, 1, 2, 0, 1, 2);
		mat3 n;
		n(0,0) =  (m(1,1) * m(2,2) - m(1,2) * m(2,1)) * det;
		n(1,0) = -(m(0,1) * m(2,2) - m(2,1) * m(0,2)) * det;
		n(2,0) =  (m(0,1) * m(1,2) - m(1,1) * m(0,2)) * det;
		n(0,1) = -(m(1,0) * m(2,2) - m(1,2) * m(2,0)) * det;
		n(1,1) =  (m(0,0) * m(2,2) - m(2,0) * m(0,2)) * det;
		n(2,1) = -(m(0,0) * m(1,2) - m(1,0) * m(0,2)) * det;
		n(0,2) =  (m(1,0) * m(2,1) - m(2,0) * m(1,1)) * det;
		n(1,2) = -(m(0,0) * m(2,1) - m(2,0) * m(0,1)) * det;
		n(2,2) =  (m(0,0) * m(1,1) - m(0,1) * m(1,0)) * det;
		return n;
	}
	inline mat4 inverse() const {
		float det= 1 / (m(0,0) * det3(1, 2, 3, 1, 2, 3) - m(0,1) * det3(0, 2, 3, 1, 2, 3)+
						m(0,2) * det3(0, 1, 3, 1, 2, 3) - m(0,3) * det3(0, 1, 2, 1, 2, 3));
		mat4 inv(0);
		inv(0,0) =  det3(1, 2, 3, 1, 2, 3) * det;
		inv(1,0) = -det3(0, 2, 3, 1, 2, 3) * det;
		inv(2,0) =  det3(0, 1, 3, 1, 2, 3) * det;
		inv(3,0) = -det3(0, 1, 2, 1, 2, 3) * det;
		inv(0,1) = -det3(1, 2, 3, 0, 2, 3) * det;
		inv(1,1) =  det3(0, 2, 3, 0, 2, 3) * det;
		inv(2,1) = -det3(0, 1, 3, 0, 2, 3) * det;
		inv(3,1) =  det3(0, 1, 2, 0, 2, 3) * det;
		inv(0,2) =  det3(1, 2, 3, 0, 1, 3) * det;
		inv(1,2) = -det3(0, 2, 3, 0, 1, 3) * det;
		inv(2,2) =  det3(0, 1, 3, 0, 1, 3) * det;
		inv(3,2) = -det3(0, 1, 2, 0, 1, 3) * det;
		inv(0,3) = -det3(1, 2, 3, 0, 1, 2) * det;
		inv(1,3) =  det3(0, 2, 3, 0, 1, 2) * det;
		inv(2,3) = -det3(0, 1, 3, 0, 1, 2) * det;
		inv(3,3) =  det3(0, 1, 2, 0, 1, 2) * det;
		return inv;
	}
	inline mat4 transpose() const { mat4 t(0); for(int i=0;i<4;i++) for(int j=0;j<4;j++) t(i,j)=m(j,i); return t; }
};
inline bool operator !=( mat4 a, mat4 b ) { for(int i=0;i<16;i++) if(a.data[i]!=b.data[i]) return true; return false; }
