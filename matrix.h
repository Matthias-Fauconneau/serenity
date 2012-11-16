#pragma once
/// file matrix.h 3x3 and 4x4 homogeneous transformation matrices
#include "vector.h"

struct mat3; inline mat3 operator*(float s, mat3 m);
/// 2D projective transformation or 3D linear transformation
struct mat3 {
    float data[3*3];
    mat3(float d=1) { for(int i=0;i<3*3;i++) data[i]=0; for(int i=0;i<3;i++) m(i,i)=d; }
    mat3(float dx, float dy) : mat3(vec3(1,0,0),vec3(0,1,0),vec3(dx,dy,1)){}
    mat3(vec3 c0, vec3 c1, vec3 c2){for(int i=0;i<3;i++) m(i,0)=c0[i], m(i,1)=c1[i], m(i,2)=c2[i]; }

    float m(int i, int j) const { return data[j*3+i]; }
    float& m(int i, int j) { return data[j*3+i]; }
    float operator()(int i, int j) const { return m(i,j); }
    float& operator()(int i, int j) { return m(i,j); }
    vec3& operator[](int j) { return (vec3&)data[j*3]; }
    const vec3& operator[](int j) const { return (vec3&)data[j*3]; }

    vec2 operator*(vec2 v) const {vec2 r(0,0); for(int i=0;i<2;i++) r[i] = v.x*m(i,0)+v.y*m(i,1)+1*m(i,2); return r; }
    vec3 operator*(vec3 v) const {vec3 r(0,0,0); for(int i=0;i<3;i++) r[i] = v.x*m(i,0)+v.y*m(i,1)+v.z*m(i,2); return r; }
    mat3 operator*(mat3 b) const {mat3 r(0); for(int j=0;j<3;j++) for(int i=0;i<3;i++) for(int k=0;k<3;k++) r.m(i,j)+=m(i,k)*b.m(k,j); return r; }

    float det() const {
        return
                m(0,0) * (m(1,1) * m(2,2) - m(2,1) * m(1,2)) -
                m(0,1) * (m(1,0) * m(2,2) - m(2,0) * m(1,2)) +
                m(0,2) * (m(1,0) * m(2,1) - m(2,0) * m(1,1));
    }
    mat3 transpose() {mat3 r(0); for(int j=0;j<3;j++) for(int i=0;i<3;i++) r(j,i)=m(i,j); return r;}
    mat3 cofactor() const {
        mat3 adj(0);
        adj(0,0) =  (m(1,1) * m(2,2) - m(2,1) * m(1,2));
        adj(1,0) = -(m(0,1) * m(2,2) - m(2,1) * m(0,2));
        adj(2,0) =  (m(0,1) * m(1,2) - m(1,1) * m(0,2));
        adj(0,1) = -(m(1,0) * m(2,2) - m(2,0) * m(1,2));
        adj(1,1) =  (m(0,0) * m(2,2) - m(2,0) * m(0,2));
        adj(2,1) = -(m(0,0) * m(1,2) - m(1,0) * m(0,2));
        adj(0,2) =  (m(1,0) * m(2,1) - m(2,0) * m(1,1));
        adj(1,2) = -(m(0,0) * m(2,1) - m(2,0) * m(0,1));
        adj(2,2) =  (m(0,0) * m(1,1) - m(0,1) * m(1,0));
        return adj;
    }
    mat3 adjugate() const { return cofactor().transpose(); }
    mat3 inverse() const { return 1/det() * adjugate() ; }

    mat3 translate(vec2 v) const { mat3 r=*this; for(int i=0;i<2;i++) r(i,2) += m(i,0)*v.x + m(i,1)*v.y; return r; }
    mat3 scale(float f) const { mat3 r=*this; for(int j=0;j<2;j++)for(int i=0;i<3;i++) r(i,j)*=f; return r; }
};
inline mat3 operator*(float s, mat3 m) {mat3 r(0); for(int j=0;j<3;j++) for(int i=0;i<3;i++) r.m(i,j)=s*m(i,j); return r; }

/// 3D projective transformation
struct mat4 {
    float data[4*4];
    mat4(int d=1) { for(int i=0;i<4*4;i++) data[i]=0; for(int i=0;i<4;i++) m(i,i)=d; }

    float m(int i, int j) const { return data[j*4+i]; }
    float& m(int i, int j) { return data[j*4+i]; }
    float operator()(int i, int j) const { return m(i,j); }
    float& operator()(int i, int j) { return m(i,j); }
    vec4& operator[](int j) { return (vec4&)data[j*4]; }

    vec4 operator*(vec3 v) const { vec4 r(0,0,0,0); for(int i=0;i<4;i++) r[i] = v.x*m(i,0)+v.y*m(i,1)+v.z*m(i,2)+1*m(i,3); return r; }
    vec4 operator*(vec4 v) const { vec4 r(0,0,0,0); for(int i=0;i<4;i++) r[i] = v.x*m(i,0)+v.y*m(i,1)+v.z*m(i,2)+v.w*m(i,3); return r; }
    mat4 operator*(mat4 b) const {
        mat4 r(0); for(int j=0;j<4;j++) for(int i=0;i<4;i++) for(int k=0;k<4;k++) r.m(i,j) += m(i,k)*b.m(k,j); return r;
    }

    float det3(int j0, int j1, int j2, int i0, int i1, int i2) const {
        return  m(i0,j0) * (m(i1,j1) * m(i2,j2) - m(i2,j1) * m(i1,j2)) -
                m(i0,j1) * (m(i1,j0) * m(i2,j2) - m(i2,j0) * m(i1,j2)) +
                m(i0,j2) * (m(i1,j0) * m(i2,j1) - m(i2,j0) * m(i1,j1));
    }
    float det() const { return m(0,0)*det3(1,2,3,1,2,3) - m(0,1)*det3(0,2,3,1,2,3)+ m(0,2)*det3(0,1,3,1,2,3) - m(0,3)*det3(0,1,2,1,2,3); }
    mat4 inverse() const {
        float idet= 1 / det();
        mat4 inv(0);
        inv(0,0) =  det3(1, 2, 3, 1, 2, 3) * idet;
        inv(1,0) = -det3(0, 2, 3, 1, 2, 3) * idet;
        inv(2,0) =  det3(0, 1, 3, 1, 2, 3) * idet;
        inv(3,0) = -det3(0, 1, 2, 1, 2, 3) * idet;
        inv(0,1) = -det3(1, 2, 3, 0, 2, 3) * idet;
        inv(1,1) =  det3(0, 2, 3, 0, 2, 3) * idet;
        inv(2,1) = -det3(0, 1, 3, 0, 2, 3) * idet;
        inv(3,1) =  det3(0, 1, 2, 0, 2, 3) * idet;
        inv(0,2) =  det3(1, 2, 3, 0, 1, 3) * idet;
        inv(1,2) = -det3(0, 2, 3, 0, 1, 3) * idet;
        inv(2,2) =  det3(0, 1, 3, 0, 1, 3) * idet;
        inv(3,2) = -det3(0, 1, 2, 0, 1, 3) * idet;
        inv(0,3) = -det3(1, 2, 3, 0, 1, 2) * idet;
        inv(1,3) =  det3(0, 2, 3, 0, 1, 2) * idet;
        inv(2,3) = -det3(0, 1, 3, 0, 1, 2) * idet;
        inv(3,3) =  det3(0, 1, 2, 0, 1, 2) * idet;
        return inv;
    }

    //transpose of the inverse of the top-left 3x3 part
    mat3 normalMatrix() const {
        float idet = 1 / det3(0, 1, 2, 0, 1, 2);
        mat3 n(0);
        n(0,0) =  (m(1,1) * m(2,2) - m(1,2) * m(2,1)) * idet;
        n(1,0) = -(m(0,1) * m(2,2) - m(2,1) * m(0,2)) * idet;
        n(2,0) =  (m(0,1) * m(1,2) - m(1,1) * m(0,2)) * idet;
        n(0,1) = -(m(1,0) * m(2,2) - m(1,2) * m(2,0)) * idet;
        n(1,1) =  (m(0,0) * m(2,2) - m(2,0) * m(0,2)) * idet;
        n(2,1) = -(m(0,0) * m(1,2) - m(1,0) * m(0,2)) * idet;
        n(0,2) =  (m(1,0) * m(2,1) - m(2,0) * m(1,1)) * idet;
        n(1,2) = -(m(0,0) * m(2,1) - m(2,0) * m(0,1)) * idet;
        n(2,2) =  (m(0,0) * m(1,1) - m(0,1) * m(1,0)) * idet;
        return n;
    }

    void translate(vec3 v) { for(int i=0;i<4;i++) m(i,3) += m(i,0)*v.x + m(i,1)*v.y + m(i,2)*v.z; }
    void scale(float f) { for(int j=0;j<3;j++) for(int i=0;i<4;i++) m(i,j)*=f; }
    void scale(vec3 v) { for(int j=0;j<3;j++) for(int i=0;i<4;i++) m(i,j)*=v[j]; }
    void rotate(float angle, vec3 u) {
        float x=u.x, y=u.y, z=u.z;
        float c=cos(angle), s=sin(angle), ic=1-c;
        mat4 r;
        r(0,0) = x*x*ic + c; r(1,0) = x*y*ic - z*s; r(2,0) = x*z*ic + y*s; r(3,0) = 0;
        r(0,1) = y*x*ic + z*s; r(1,1) = y*y*ic + c; r(2,1) = y*z*ic - x*s; r(3,1) = 0;
        r(0,2) = x*z*ic - y*s; r(1,2) = y*z*ic + x*s; r(2,2) = z*z*ic + c; r(3,2) = 0;
        r(0,3) = 0; r(1,3) = 0; r(2,3) = 0; r(3,3) = 1;
        *this = *this * r;
    }
    void rotateX(float angle) { float c=cos(angle),s=sin(angle); mat4 r; r.m(1,1) = c; r.m(2,2) = c; r.m(1,2) = -s; r.m(2,1) = s; *this = *this * r; }
    void rotateY(float angle) { float c=cos(angle),s=sin(angle); mat4 r; r.m(0,0) = c; r.m(2,2) = c; r.m(2,0) = -s; r.m(0,2) = s; *this = *this * r; }
    void rotateZ(float angle) { float c=cos(angle),s=sin(angle); mat4 r; r.m(0,0) = c; r.m(1,1) = c; r.m(0,1) = -s; r.m(1,0) = s; *this = *this * r; }
};

template<int M, int N> inline string str(const float a[M*N]) {
    string s; s<<"\n["_;
    for(int i=0;i<M;i++) {
        if(N==1) s = s+"\t"_+str(a[i]);
        else {
            for(int j=0;j<N;j++) {
                s = s+"\t"_+str(a[j*M+i]);
            }
            if(i<M-1) s=s+"\n"_;
        }
    }
    s<<" ]"_;
    return s;
}
inline string str(const mat3& m) { return str<3,3>(m.data); }
inline string str(const mat4& m) { return str<4,4>(m.data); }
