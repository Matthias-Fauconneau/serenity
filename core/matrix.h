#pragma once
/// file matrix.h 3x3 homogeneous transformation matrix
#include "vector.h"

struct mat3; inline mat3 operator*(float s, mat3 M);
/// 2D projective transformation or 3D linear transformation
struct mat3 {
    float data[3*3];
    mat3(float d=1) { for(int i=0;i<3*3;i++) data[i]=0; for(int i=0;i<3;i++) M(i,i)=d; }
    mat3(float dx, float dy) : mat3(vec3(1,0,0),vec3(0,1,0),vec3(dx,dy,1)){}
    mat3(vec3 e0, vec3 e1, vec3 e2){for(int i=0;i<3;i++) M(i,0)=e0[i], M(i,1)=e1[i], M(i,2)=e2[i]; }

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
inline string str(const mat3& M) { return str<3,3>(M.data); }
