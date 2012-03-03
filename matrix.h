#pragma once
#include "vector.h"

/// Fixed-size transformation matrices (using GLSL conventions)

struct mat2 {
    float m11, m12, m21, m22;
    mat2(float m11, float m12, float m21, float m22):m11(m11),m12(m12),m21(m21),m22(m22){}
    mat2() : mat2(1,0,0,1) {}
    vec2 operator*(vec2 v) { return vec2( m11*v.x + m21*v.y, m12*v.x + m22*v.y ); }
    mat2 operator*(mat2 m) const { return mat2( m11*m.m11 + m12*m.m21, m11*m.m12 + m12*m.m22,
                                                m21*m.m11 + m22*m.m21, m21*m.m12 + m22*m.m22); }
};

struct mat32 {
    float m11, m12, m21, m22, dx, dy;
    mat32(float m11, float m12, float m21, float m22, float dx, float dy):m11(m11),m12(m12),m21(m21),m22(m22),dx(dx),dy(dy){}
    mat32(float dx, float dy) : mat32(1,0,0,1,dx,dy) {}
    mat32(vec2 t) : mat32(1,0,0,1,t.x,t.y) {}
    mat32() : mat32(1,0,0,1,0,0) {}
    mat32 operator*(mat32 m) const { return mat32( m11*m.m11 + m12*m.m21, m11*m.m12 + m12*m.m22,
                                                          m21*m.m11 + m22*m.m21, m21*m.m12 + m22*m.m22,
                                                          dx*m.m11  + dy*m.m21 + m.dx, dx*m.m12  + dy*m.m22 + m.dy ); }
    vec2 operator*(vec2 v) const { return vec2( m11*v.x + m21*v.y + dx, m12*v.x + m22*v.y + dy ); }
};

struct mat3 {
    float data[3*3];
    float& m(int i, int j) { return data[j*3+i]; }
    float& operator()(int i, int j) { return m(i,j); }
    vec3 operator*(vec3 v) { vec3 r(0,0,0); for(int i=0;i<3;i++) r[i] = v.x*m(i,0)+v.y*m(i,1)+v.z*m(i,2); return r; }
};

struct mat4 {
    float data[4*4];
    mat4(int d=1) { for(int i=0;i<16;i++) data[i]=0; if(d!=0) for(int i=0;i<4;i++) m(i,i)=d; }
    float m(int i, int j) const { return data[j*4+i]; }
    float& m(int i, int j) { return data[j*4+i]; }
    float operator()(int i, int j) const { return m(i,j); }
    float& operator()(int i, int j) { return m(i,j); }
    vec4 operator*(vec4 v) const { vec4 r(0,0,0,0); for(int i=0;i<4;i++) r[i] = v.x*m(i,0)+v.y*m(i,1)+v.z*m(i,2)+v.w*m(i,3); return r; }
    mat4 operator*(mat4 b) const {
        mat4 r(0); for(int j=0;j<4;j++) for(int i=0;i<4;i++) for(int k=0;k<4;k++) r.m(i,j) += m(i,k)*b.m(k,j); return r;
    }
    void translate(vec3 v) { for(int i=0;i<4;i++) m(i,3) += m(i,0)*v.x + m(i,1)*v.y + m(i,2)*v.z; }
    void scale(float f) { for(int j=0;j<3;j++) for(int i=0;i<4;i++) m(i,j)*=f; }
    void scale(vec3 v) { for(int j=0;j<3;j++) for(int i=0;i<4;i++) m(i,j)*=v[j]; }
    void rotateX( float angle ) {
        float c=cos(angle),s=sin(angle); mat4 r; r.m(1,1) = c; r.m(2,2) = c; r.m(1,2) = -s; r.m(2,1) = s; *this = *this * r;
    }
    void rotateY( float angle ) {
        float c=cos(angle),s=sin(angle); mat4 r; r.m(0,0) = c; r.m(2,2) = c; r.m(2,0) = -s; r.m(0,2) = s; *this = *this * r;
    }
    void rotateZ( float angle ) {
        float c=cos(angle),s=sin(angle); mat4 r; r.m(0,0) = c; r.m(1,1) = c; r.m(0,1) = -s; r.m(1,0) = s; *this = *this * r;
    }
    float det3(int j0, int j1, int j2, int i0, int i1, int i2) const {
        return  m(i0,j0) * (m(i1,j1) * m(i2,j2) - m(i2,j1) * m(i1,j2)) -
                m(i0,j1) * (m(i1,j0) * m(i2,j2) - m(i2,j0) * m(i1,j2)) +
                m(i0,j2) * (m(i1,j0) * m(i2,j1) - m(i2,j0) * m(i1,j1));
    }
    mat3 normalMatrix() const {
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
    mat4 inverse() const {
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
    mat4 transpose() const { mat4 t(0); for(int i=0;i<4;i++) for(int j=0;j<4;j++) t(i,j)=m(j,i); return t; }
};
inline bool operator !=( mat4 a, mat4 b ) { for(int i=0;i<16;i++) if(a.data[i]!=b.data[i]) return true; return false; }
