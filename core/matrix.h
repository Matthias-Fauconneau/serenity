#pragma once
/// file matrix.h 3x3 homogeneous transformation matrix
#include "vector.h"

/// 2D affine transformation
struct mat3x2 {
    float data[3*2];
    mat3x2(float d=1) : data{d,0,0, 0,d,0} {}
    mat3x2(float dx, float dy) : data{1,0,dx, 0,1,dy} {}
    mat3x2(float m11, float m12, float dx, float m21, float m22, float dy):data{m11,m12,dx,m21,m22,dy}{}

    float M(int i, int j) const { return data[j*2+i]; }
    float& M(int i, int j) { return data[j*2+i]; }
    float operator()(int i, int j) const { return M(i,j); }
    float& operator()(int i, int j) { return M(i,j); }

    mat3x2 operator*(mat3x2 b) const {mat3x2 r(0); for(int i=0;i<2;i++) { for(int j=0;j<3;j++) for(int k=0;k<2;k++) r.M(i,j)+=M(i,k)*b.M(k,j); r.M(i,2)+=M(i,2); } return r; }
    vec2 operator*(vec2 v) const {vec2 r; for(int i=0;i<2;i++) r[i] = v.x*M(i,0)+v.y*M(i,1)+1*M(i,2); return r; }
};
inline bool operator !=(const mat3x2& a, const mat3x2& b) { for(int i=0;i<6;i++) if(a.data[i]!=b.data[i]) return true; return false; }

struct mat3; inline mat3 operator*(float s, mat3 M);
/// 2D projective transformation or 3D linear transformation
struct mat3 {
    float data[3*3];
    mat3(float d=1) : data{d,0,0, 0,d,0, 0,0,d} {}
    mat3(float dx, float dy) : data{1,0,0, 0,1,0, dx,dy,1} {}
    //mat3(vec3 e0, vec3 e1, vec3 e2){for(int i=0;i<3;i++) M(i,0)=e0[i], M(i,1)=e1[i], M(i,2)=e2[i]; }

    float M(int i, int j) const { return data[j*3+i]; }
    float& M(int i, int j) { return data[j*3+i]; }
    float operator()(int i, int j) const { return M(i,j); }
    float& operator()(int i, int j) { return M(i,j); }
    vec3& operator[](int j) { return (vec3&)data[j*3]; }
    const vec3& operator[](int j) const { return (vec3&)data[j*3]; }

    vec2 operator*(vec2 v) const {vec2 r; for(int i=0;i<2;i++) r[i] = v.x*M(i,0)+v.y*M(i,1)+1*M(i,2); return r; }
    vec3 operator*(vec3 v) const {vec3 r; for(int i=0;i<3;i++) r[i] = v.x*M(i,0)+v.y*M(i,1)+v.z*M(i,2); return r; }
    mat3 operator*(mat3 b) const {mat3 r(0); for(int j=0;j<3;j++) for(int i=0;i<3;i++) for(int k=0;k<3;k++) r.M(i,j)+=M(i,k)*b.M(k,j); return r; }

    float det() const {
        return
                M(0,0) * (M(1,1) * M(2,2) - M(2,1) * M(1,2)) -
                M(0,1) * (M(1,0) * M(2,2) - M(2,0) * M(1,2)) +
                M(0,2) * (M(1,0) * M(2,1) - M(2,0) * M(1,1));
    }
    mat3 transpose() {mat3 r; for(int j=0;j<3;j++) for(int i=0;i<3;i++) r(j,i)=M(i,j); return r;}
    mat3 cofactor() const {
        mat3 C;
        C(0,0) =  (M(1,1) * M(2,2) - M(2,1) * M(1,2)), C(0,1) = -(M(1,0) * M(2,2) - M(2,0) * M(1,2)), C(0,2) =  (M(1,0) * M(2,1) - M(2,0) * M(1,1));
        C(1,0) = -(M(0,1) * M(2,2) - M(2,1) * M(0,2)), C(1,1) =  (M(0,0) * M(2,2) - M(2,0) * M(0,2)), C(1,2) = -(M(0,0) * M(2,1) - M(2,0) * M(0,1));
        C(2,0) =  (M(0,1) * M(1,2) - M(1,1) * M(0,2)), C(2,1) = -(M(0,0) * M(1,2) - M(1,0) * M(0,2)), C(2,2) =  (M(0,0) * M(1,1) - M(0,1) * M(1,0));
        return C;
    }
    mat3 adjugate() const { return cofactor().transpose(); }
    mat3 inverse() const { return 1/det() * adjugate() ; }

    mat3 translate(vec2 v) const { mat3 r=*this; for(int i=0;i<2;i++) r(i,2) += M(i,0)*v.x + M(i,1)*v.y; return r; }
    mat3 scale(float f) const { mat3 r=*this; for(int j=0;j<2;j++)for(int i=0;i<3;i++) r(i,j)*=f; return r; }
    void rotateX(float angle) { float c=cos(angle),s=sin(angle); mat3 r; r.M(1,1) = c; r.M(2,2) = c; r.M(1,2) = -s; r.M(2,1) = s; *this = *this * r; }
    void rotateY(float angle) { float c=cos(angle),s=sin(angle); mat3 r; r.M(0,0) = c; r.M(2,2) = c; r.M(2,0) = -s; r.M(0,2) = s; *this = *this * r; }
    void rotateZ(float angle) { float c=cos(angle),s=sin(angle); mat3 r; r.M(0,0) = c; r.M(1,1) = c; r.M(0,1) = -s; r.M(1,0) = s; *this = *this * r; }
};
inline mat3 operator*(float s, mat3 M) {mat3 r; for(int j=0;j<3;j++) for(int i=0;i<3;i++) r.M(i,j)=s*M(i,j); return r; }

struct mat4; inline mat4 operator*(float s, mat4 M);
/// 3D projective transformation
struct mat4 {
    float data[4*4];
    mat4(int d=1) { for(int i=0;i<4*4;i++) data[i]=0; for(int i=0;i<4;i++) M(i,i)=d; }
    mat4(mat3 m):mat4(1){for(int i=0;i<3;i++) for(int j=0;j<3;j++) M(i,j)=m(i,j); }

    float M(int i, int j) const { return data[j*4+i]; }
    float& M(int i, int j) { return data[j*4+i]; }
    float operator()(int i, int j) const { return M(i,j); }
    float& operator()(int i, int j) { return M(i,j); }
    vec4& operator[](int j) { return (vec4&)data[j*4]; }
    const vec4& operator[](int j) const { return (vec4&)data[j*4]; }

    vec3 operator*(vec3 v) const { vec3 r; for(int i=0;i<3;i++) r[i] = v.x*M(i,0)+v.y*M(i,1)+v.z*M(i,2)+1*M(i,3); return r; }
    vec4 operator*(vec4 v) const { vec4 r; for(int i=0;i<4;i++) r[i] = v.x*M(i,0)+v.y*M(i,1)+v.z*M(i,2)+v.w*M(i,3); return r; }
    mat4 operator*(mat4 b) const{mat4 r(0); for(int j=0;j<4;j++) for(int i=0;i<4;i++) for(int k=0;k<4;k++) r.M(i,j) += M(i,k)*b.M(k,j); return r; }

    float minor(int j0, int j1, int j2, int i0, int i1, int i2) const {
        return
                M(i0,j0) * (M(i1,j1) * M(i2,j2) - M(i2,j1) * M(i1,j2)) -
                M(i0,j1) * (M(i1,j0) * M(i2,j2) - M(i2,j0) * M(i1,j2)) +
                M(i0,j2) * (M(i1,j0) * M(i2,j1) - M(i2,j0) * M(i1,j1));
    }
    float det() const { return M(0,0)*minor(1,2,3,1,2,3) - M(0,1)*minor(0,2,3,1,2,3)+ M(0,2)*minor(0,1,3,1,2,3) - M(0,3)*minor(0,1,2,1,2,3); }
    mat4 cofactor() const {
        mat4 C;
        C(0,0) =  minor(1, 2, 3, 1, 2, 3), C(0,1) = -minor(0, 2, 3, 1, 2, 3), C(0,2) =  minor(0, 1, 3, 1, 2, 3), C(0,3) = -minor(0, 1, 2, 1, 2, 3);
        C(1,0) = -minor(1, 2, 3, 0, 2, 3), C(1,1) =  minor(0, 2, 3, 0, 2, 3), C(1,2) = -minor(0, 1, 3, 0, 2, 3), C(1,3) =  minor(0, 1, 2, 0, 2, 3);
        C(2,0) =  minor(1, 2, 3, 0, 1, 3), C(2,1) = -minor(0, 2, 3, 0, 1, 3), C(2,2) =  minor(0, 1, 3, 0, 1, 3), C(2,3) = -minor(0, 1, 2, 0, 1, 3);
        C(3,0) = -minor(1, 2, 3, 0, 1, 2), C(3,1) =  minor(0, 2, 3, 0, 1, 2), C(3,2) = -minor(0, 1, 3, 0, 1, 2), C(3,3) =  minor(0, 1, 2, 0, 1, 2);
        return C;
    }
    mat4 transpose() {mat4 r; for(int j=0;j<4;j++) for(int i=0;i<4;i++) r(j,i)=M(i,j); return r;}
    mat4 adjugate() const { return cofactor().transpose(); }
    mat4 inverse() const { return 1/det() * adjugate() ; }

    //top-left 3x3 part of the transpose of the inverse
    mat3 normalMatrix() const { return (mat3)inverse().transpose(); }
    explicit operator mat3() const {
        mat3 r;
        for(int i=0;i<3;i++) for(int j=0;j<3;j++) r(i,j)=M(i,j);
        return r;
    }

    inline void perspective(float hfov, float width, float height, float nearPlane, float farPlane) {
        float cotan = cos(hfov/2) / sin(hfov/2);
        M(0,0) = cotan * height / width; M(1,1) = cotan; M(2,2) = (nearPlane+farPlane) / (nearPlane-farPlane);
        M(2,3) = (2*nearPlane*farPlane) / (nearPlane-farPlane); M(3,2) = -1; M(3,3) = 0;
    }
    void translate(vec3 v) { for(int i=0;i<4;i++) M(i,3) += M(i,0)*v.x + M(i,1)*v.y + M(i,2)*v.z; }
    void scale(float f) { for(int j=0;j<3;j++) for(int i=0;i<4;i++) M(i,j)*=f; }
    void scale(vec3 v) { for(int j=0;j<3;j++) for(int i=0;i<4;i++) M(i,j)*=v[j]; }
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
    void rotateX(float angle) { float c=cos(angle),s=sin(angle); mat4 r; r.M(1,1) = c; r.M(2,2) = c; r.M(1,2) = -s; r.M(2,1) = s; *this = *this * r; }
    void rotateY(float angle) { float c=cos(angle),s=sin(angle); mat4 r; r.M(0,0) = c; r.M(2,2) = c; r.M(2,0) = -s; r.M(0,2) = s; *this = *this * r; }
    void rotateZ(float angle) { float c=cos(angle),s=sin(angle); mat4 r; r.M(0,0) = c; r.M(1,1) = c; r.M(0,1) = -s; r.M(1,0) = s; *this = *this * r; }
};
inline mat4 operator*(float s, mat4 M) {mat4 r; for(int j=0;j<4;j++) for(int i=0;i<4;i++) r.M(i,j)=s*M(i,j); return r; }
inline bool operator !=( mat4 a, mat4 b ) { for(int i=0;i<16;i++) if(a.data[i]!=b.data[i]) return true; return false; }

template<int M, int N> inline String str(const float a[M*N]) {
    String s; s<<"\n["_;
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
inline String str(const mat3& M) { return str<3,3>(M.data); }
inline String str(const mat4& M) { return str<4,4>(M.data); }
