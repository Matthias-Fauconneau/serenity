#pragma once
/// file matrix.h 3x3 homogeneous transformation matrix
#include "vector.h"
#include "math.h"

inline struct mat2 operator*(float s, mat2 M);
/// 2D linear transformation
struct mat2 {
    float data[2*2];

    explicit mat2(vec2 d=1_) { for(int i: range(2*2)) data[i]=0; for(int i: range(2)) M(i,i)=d[i]; }
    mat2(vec2 e0, vec2 e1) { for(int i: range(3)) { M(i,0)=e0[i]; M(i,1)=e1[i]; } }

    float M(int i, int j) const { return data[j*2+i]; }
    float& M(int i, int j) { return data[j*2+i]; }
    float operator()(int i, int j) const { return M(i,j); }
    float& operator()(int i, int j) { return M(i,j); }

    vec2 operator*(vec2 v) const {vec2 r; for(int i: range(2)) r[i] = v.x*M(i,0)+v.y*M(i,1); return r; }
    mat2 operator*(mat2 b) const {
        mat2 r(0_);
        for(int j: range(2)) for(int i: range(2)) for(int k: range(2)) r(i,j)+=M(i,k)*b(k,j);
        return r;
    }

    float det() const { return M(0,0) * M(1,1) - M(0,1) * M(1,0); }
    mat2 transpose() const { mat2 r; for(int j: range(2)) for(int i: range(2)) r(j,i)=M(i,j); return r;}
    mat2 cofactor() const {
        mat2 C;
        C(0,0) = +M(1,1); C(0,1) = -M(1,0);
        C(1,0) = -M(0,1); C(1,1) = +M(0,0);
        return C;
    }
    mat2 adjugate() const { return cofactor().transpose(); }
    mat2 inverse() const { return 1/det() * adjugate() ; }
};
inline mat2 operator*(float s, mat2 M) {
    mat2 r;
    for(int j: range(2)) for(int i: range(2)) r(i,j) = s * M(i,j);
    return r;
}

#if 1
inline struct mat3 operator*(float s, mat3 M);
/// 2D projective transformation or 3D linear transformation
struct mat3 {
    float data[3*3];

    explicit mat3(vec3 d=1_) { for(int i: range(3*3)) data[i]=0; for(int i: range(3)) M(i,i)=d[i]; }
    mat3(vec3 e0, vec3 e1, vec3 e2) { for(int i: range(3)) { M(i,0)=e0[i]; M(i,1)=e1[i]; M(i,2)=e2[i]; } }

    float M(int i, int j) const { return data[j*3+i]; }
    float& M(int i, int j) { return data[j*3+i]; }
    float operator()(int i, int j) const { return M(i,j); }
    float& operator()(int i, int j) { return M(i,j); }
    vec3& operator[](int j) { return (vec3&)data[j*3]; }
    const vec3& operator[](int j) const { return *reinterpret_cast<const vec3*>(&data[j*3]); }

    //vec2 operator*(vec2 v) const { vec2 r; for(int i: range(2)) r[i] = v.x*M(i,0)+v.y*M(i,1)+1*M(i,2); return r; }
    vec2 operator*(vec2 v) const { vec3 r; for(int i: range(3)) r[i] = v.x*M(i,0)+v.y*M(i,1)+1*M(i,2); return r.xy()/r.z; }
    vec3 operator*(vec3 v) const { vec3 r; for(int i: range(3)) r[i] = v.x*M(i,0)+v.y*M(i,1)+v.z*M(i,2); return r; }
    mat3 operator*(mat3 b) const {
        mat3 r(0_);
        for(int j: range(3)) for(int i: range(3)) for(int k: range(3)) r(i,j)+=M(i,k)*b(k,j);
        return r;
    }

    float det() const {
        return
                M(0,0) * (M(1,1) * M(2,2) - M(2,1) * M(1,2)) -
                M(0,1) * (M(1,0) * M(2,2) - M(2,0) * M(1,2)) +
                M(0,2) * (M(1,0) * M(2,1) - M(2,0) * M(1,1));
    }
    mat3 transpose() {mat3 r; for(int j: range(3)) for(int i: range(3)) r(j,i)=M(i,j); return r;}
    mat3 cofactor() const {
        mat3 C;
        C(0,0) =  (M(1,1)*M(2,2) - M(2,1)*M(1,2)); C(0,1) = -(M(1,0)*M(2,2) - M(2,0)*M(1,2)); C(0,2) =  (M(1,0)*M(2,1) - M(2,0)*M(1,1));
        C(1,0) = -(M(0,1)*M(2,2) - M(2,1)*M(0,2)); C(1,1) =  (M(0,0)*M(2,2) - M(2,0)*M(0,2)); C(1,2) = -(M(0,0)*M(2,1) - M(2,0)*M(0,1));
        C(2,0) =  (M(0,1)*M(1,2) - M(1,1)*M(0,2)); C(2,1) = -(M(0,0)*M(1,2) - M(1,0)*M(0,2)); C(2,2) =  (M(0,0)*M(1,1) - M(0,1)*M(1,0));
        return C;
    }
    mat3 adjugate() const { return cofactor().transpose(); }
    mat3 ¯¹() const { return 1/det() * adjugate() ; }
};
inline mat3 operator*(float s, mat3 M) {
    mat3 r;
    for(int j: range(3)) for(int i: range(3)) r(i,j) = s * M(i,j);
    return r;
}
inline mat3 operator-(float s, mat3 M) {
    for(int j: range(3)) for(int i: range(3)) M(i,j) = -M(i,j);
    for(int i: range(3)) M(i,i) += s;
    return M;
}
inline mat3 outer(vec3 a, vec3 b) {
    mat3 r;
    for(int j: range(3)) for(int i: range(3)) r(i,j)=a[i]*b[j];
    return r;
}
inline mat3 operator+(mat3 a, mat3 b) {
    mat3 r;
    for(int j : range(3)) for(int i: range(3)) r(i,j)=a(i,j)+b(i,j);
    return r;
}

inline struct mat4 operator*(float s, mat4 M);
/// 3D projective transformation
struct mat4 {
    float data[4*4];

    mat4(vec4 d=1_) { for(int i: range(4*4)) data[i]=0; for(int i: range(4)) M(i,i)=d[i]; }
    mat4(mat3 m) : mat4(1_) { for(int i: range(3)) for(int j: range(3)) M(i,j)=m(i,j); }

    float M(int i, int j) const { return data[j*4+i]; }
    float& M(int i, int j) { return data[j*4+i]; }
    float operator()(int i, int j) const { return M(i,j); }
    float& operator()(int i, int j) { return M(i,j); }
    vec4& operator[](int j) { return (vec4&)data[j*4]; }
    const vec4& operator[](int j) const { return *reinterpret_cast<const vec4*>(&data[j*4]); }

    vec3 operator*(vec3 v) const { vec4 r; for(int i: range(4)) r[i] = v.x*M(i,0)+v.y*M(i,1)+v.z*M(i,2)+1*M(i,3); return r.xyz()/r.w; }
    vec4 operator*(vec4 v) const { vec4 r; for(int i: range(4)) r[i] = v.x*M(i,0)+v.y*M(i,1)+v.z*M(i,2)+v.w*M(i,3); return r; }
    mat4 operator*(mat4 b) const { mat4 r(0_); for(int j: range(4)) for(int i: range(4)) for(int k: range(4)) r(i,j) += M(i,k)*b(k,j); return r; }
#undef minor
    float minor(int j0, int j1, int j2, int i0, int i1, int i2) const {
        return
                M(i0,j0) * (M(i1,j1) * M(i2,j2) - M(i2,j1) * M(i1,j2)) -
                M(i0,j1) * (M(i1,j0) * M(i2,j2) - M(i2,j0) * M(i1,j2)) +
                M(i0,j2) * (M(i1,j0) * M(i2,j1) - M(i2,j0) * M(i1,j1));
    }
    float det() const { return M(0,0)*minor(1,2,3,1,2,3) - M(0,1)*minor(0,2,3,1,2,3)+ M(0,2)*minor(0,1,3,1,2,3) - M(0,3)*minor(0,1,2,1,2,3); }
    mat4 cofactor() const {
        mat4 C;
        C(0,0) =  minor(1, 2, 3, 1, 2, 3); C(0,1) = -minor(0, 2, 3, 1, 2, 3); C(0,2) =  minor(0, 1, 3, 1, 2, 3); C(0,3) = -minor(0, 1, 2, 1, 2, 3);
        C(1,0) = -minor(1, 2, 3, 0, 2, 3); C(1,1) =  minor(0, 2, 3, 0, 2, 3); C(1,2) = -minor(0, 1, 3, 0, 2, 3); C(1,3) =  minor(0, 1, 2, 0, 2, 3);
        C(2,0) =  minor(1, 2, 3, 0, 1, 3); C(2,1) = -minor(0, 2, 3, 0, 1, 3); C(2,2) =  minor(0, 1, 3, 0, 1, 3); C(2,3) = -minor(0, 1, 2, 0, 1, 3);
        C(3,0) = -minor(1, 2, 3, 0, 1, 2); C(3,1) =  minor(0, 2, 3, 0, 1, 2); C(3,2) = -minor(0, 1, 3, 0, 1, 2); C(3,3) =  minor(0, 1, 2, 0, 1, 2);
        return C;
    }
    mat4 ᵀ() const {mat4 r; for(int j=0;j<4;j++) for(int i=0;i<4;i++) r(j,i)=M(i,j); return r;}
    mat4 adjugate() const { return cofactor().ᵀ(); }
    mat4 ¯¹() const { return 1/det() * adjugate() ; }
    explicit operator mat3() const {
        mat3 r;
        for(int i=0;i<3;i++) for(int j=0;j<3;j++) r(i,j)=M(i,j);
        return r;
    }
    mat3 normalMatrix() const { return (mat3)(¯¹().ᵀ()); }

    mat4& translate(vec3 v) { for(int i=0;i<4;i++) M(i,3) += M(i,0)*v.x + M(i,1)*v.y + M(i,2)*v.z; return *this; }
    mat4& scale(vec3 v) { for(int j=0;j<3;j++) for(int i=0;i<4;i++) M(i,j)*=v[j]; return *this; }
    mat4& rotate(float angle, vec3 u) {
        float x=u.x, y=u.y, z=u.z;
        float c=cos(angle), s=sin(angle), ic=1-c;
        mat4 r;
        r(0,0) = x*x*ic + c; r(1,0) = x*y*ic - z*s; r(2,0) = x*z*ic + y*s; r(3,0) = 0;
        r(0,1) = y*x*ic + z*s; r(1,1) = y*y*ic + c; r(2,1) = y*z*ic - x*s; r(3,1) = 0;
        r(0,2) = x*z*ic - y*s; r(1,2) = y*z*ic + x*s; r(2,2) = z*z*ic + c; r(3,2) = 0;
        r(0,3) = 0; r(1,3) = 0; r(2,3) = 0; r(3,3) = 1;
        return *this = *this * r;
    }
    mat4& rotateX(float angle) { float c=cos(angle),s=sin(angle); mat4 r; r(1,1) = c; r(2,2) = c; r(1,2) = -s; r(2,1) = s; return *this = *this * r; }
    mat4& rotateY(float angle) { float c=cos(angle),s=sin(angle); mat4 r; r(0,0) = c; r(2,2) = c; r(2,0) = -s; r(0,2) = s; return *this = *this * r; }
    mat4& rotateZ(float angle) { float c=cos(angle),s=sin(angle); mat4 r; r(0,0) = c; r(1,1) = c; r(0,1) = -s; r(1,0) = s; return *this = *this * r; }
};
inline mat4 operator*(float s, mat4 M) {mat4 r; for(int j=0;j<4;j++) for(int i=0;i<4;i++) r(i,j)=s*M(i,j); return r; }

template<int N, int M, Type T> inline String str(const T a[M*N]) {
    array<char> s;
    for(int i=0;i<M;i++) {
        if(N==1) s.append("\t"+fmt(a[i], 4, 0, 9));
        else {
            for(int j=0;j<N;j++) {
                s.append("\t"+fmt(a[j*M+i], 4, 0, 9));
            }
            if(i<M-1) s.append('\n');
        }
    }
    return move(s);
}
inline String str(const mat3& M) { return str<3,3>(M.data); }
inline String str(const mat4& M) { return str<4,4>(M.data); }

inline mat4 perspective(const float near, const float far, const float s=0, const float t=0) { // Sheared perspective (rectification)
    const float left = (-1-s), right = (1-s);
    const float bottom = (-1-t), top = (1-t);
    mat4 M;
    M(0,0) = 2*near / (right-left);
    M(1,1) = 2*near / (top-bottom);
    M(0,2) = (right+left) / (right-left);
    M(1,2) = (top+bottom) / (top-bottom);
    M(2,2) = - (far+near) / (far-near);
    M(2,3) = - 2*far*near / (far-near);
    M(3,2) = - 1;
    M(3,3) = 0;
    M.translate(vec3(-s,-t,0));
    return M;
}
#endif
