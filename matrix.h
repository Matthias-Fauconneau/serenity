#pragma once
/// file matrix.h 2D linear and affine transformation matrices
#include "vector.h"

/// 2D linear transformation
struct mat2 {
    float m11, m12, m21, m22;
    mat2(float m11, float m12, float m21, float m22):m11(m11),m12(m12),m21(m21),m22(m22){}
    mat2() : mat2(1,0,0,1) {}
    vec2 operator*(vec2 v) { return vec2( m11*v.x + m21*v.y, m12*v.x + m22*v.y ); }
    mat2 operator*(mat2 m) const { return mat2( m11*m.m11 + m12*m.m21, m11*m.m12 + m12*m.m22,
                                                m21*m.m11 + m22*m.m21, m21*m.m12 + m22*m.m22); }
};

/// 2D affine transformation
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
