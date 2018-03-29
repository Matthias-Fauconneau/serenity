#include "Rpoly.h"
#include <opencv2/core/core.hpp> // opencv_core
#undef assert
#include "RPP.h"
#include <vector>
typedef cv::Mat_<double> Mat;
// Schweighofer G., Pinz A. : Robust Pose Estimation from a Planar Target. C++ port by Nghia Ho

struct Quaternion {
    cv::Vec3d vector;
    double scalar;

    Quaternion(){}
    Quaternion(const cv::Vec3d& v, double s) {
        vector = v;
        scalar = s;
    }
};

struct Solution {
    Solution() {
        R = Mat::zeros(3,3);
        t = Mat::zeros(3,1);
    }

    //no_copy(Solution);
    /*Solution(const Solution& s) {
        R = s.R.clone();
        t = s.t.clone();
        E = s.E;
        bl = s.bl;
        at = s.at;
        obj_err = s.obj_err;
        img_err = s.img_err;
    }*/


    Mat R;
    Mat t;
    double E;
    double bl;
    double at;
    double obj_err;
    double img_err;
};

inline Quaternion Quaternion_byVectorAndScalar(const cv::Vec3d& vector, double scalar)
{
    return Quaternion(vector, scalar);
}

inline Quaternion Quaternion_multiplyByScalar(const Quaternion& q, double scalar)
{
    Quaternion ret;

    ret.vector[0] = q.vector[0]*scalar;
    ret.vector[1] = q.vector[1]*scalar;
    ret.vector[2] = q.vector[2]*scalar;

    ret.scalar = q.scalar*scalar;

    return ret;
}

inline double Quaternion_Norm(const Quaternion& Q)
{
    return sqrt(Q.vector[0]*Q.vector[0] + Q.vector[1]*Q.vector[1] + Q.vector[2]*Q.vector[2] + Q.scalar*Q.scalar);
}

inline int sign(double x)
{
    if(x < 0)
        return -1;

    if(x > 0)
        return 1;

    return 0;
}

#include <cstdio>

//#define _USE_MATH_DEFINES
#include <math.h>

using namespace std;
//using namespace cv;

static double TOL = 1E-5;
static double EPSILON = 1E-8;


static inline Mat EstimateT(const Mat& R, const Mat& G, const vector <Mat>& F, const Mat& P)
{
    Mat sum = Mat::zeros(3,1);

    Mat PP(3,1);

    for(int i=0; i < P.cols; i++) {
        PP(0,0) = P(0,i);
        PP(1,0) = P(1,i);
        PP(2,0) = P(2,i);

        sum += F[i]*R*PP;
    }

    Mat ret = G*sum;

    return ret;
}


Mat NormRv(const Mat& R)
{
    Mat ret(R.rows, R.cols);

    for(int i=0; i < R.cols; i++) {

        double mag = R(0,i)*R(0,i) +
                    R(1,i)*R(1,i) +
                    R(2,i)*R(2,i);

        double m = 1.f/sqrt(mag);

        ret(0,i) = R(0,i)*m;
        ret(1,i) = R(1,i)*m;
        ret(2,i) = R(2,i)*m;
    }

    return ret;
}

Mat NormRv(const cv::Vec3d& V)
{
    Mat ret(3,1);

    double mag = sqrt(V[0]*V[0] + V[1]*V[1] + V[2]*V[2]);

    ret(0,0) = V[0] / mag;
    ret(1,0) = V[1] / mag;
    ret(2,0) = V[2] / mag;

    return ret;
}



Mat quat2mat(const Quaternion& Q)
{
    double a = Q.scalar;
    double b = Q.vector[0];
    double c = Q.vector[1];
    double d = Q.vector[2];

    Mat R(3,3);

    R(0,0) = a*a + b*b - c*c -d*d;
    R(0,1) = 2*(b*c - a*d);
    R(0,2) = 2*(b*d + a*c);

    R(1,0) = 2*(b*c + a*d);
    R(1,1) = a*a + c*c - b*b - d*d;
    R(1,2) = 2*(c*d - a*b);

    R(2,0) = 2*(b*d - a*c);
    R(2,1) = 2*(c*d + a*b);
    R(2,2) = a*a + d*d - b*b -c*c;

    return R;
}

static Mat Vec2Mat(const cv::Vec3d& v)
{
    Mat ret(3,1);

    ret(0,0) = v[0];
    ret(1,0) = v[1];
    ret(2,0) = v[2];

    return ret;
}

double Norm(const Mat& m)
{
    cv::SVD decomp(m);

    return decomp.w.at<double>(0,0);
}

static Quaternion Quaternion_byAngleAndVector(double q_angle, const cv::Vec3d& q_vector)
{
    //printf("Quaternion_byAngleAndVector\n");

    // rotation_axis=q_vector/norm(q_vector);
    // matlab norm with default argument of 2 is largest singular value
    double n = Norm(Vec2Mat(q_vector));

    //printf("n = %f\n", n);

    cv::Vec3d rotation_axis = q_vector;

    rotation_axis[0] /= n;
    rotation_axis[1] /= n;
    rotation_axis[2] /= n;

    rotation_axis[0] *= sin(q_angle*0.5);
    rotation_axis[1] *= sin(q_angle*0.5);
    rotation_axis[2] *= sin(q_angle*0.5);

    Quaternion Q_ = Quaternion_byVectorAndScalar(rotation_axis,	cos(q_angle*0.5));

    //printf("Q_\n");
    //Print(Q_);

//    double a = 1/Quaternion_Norm(Q_);

    //printf("a = %f\n", a);

    Quaternion Q = Quaternion_multiplyByScalar(Q_, 1/Quaternion_Norm(Q_));

    return Q;
}

Mat Mul(const Mat& a, const Mat& b)
{
    assert(a.rows == b.rows && a.cols == b.cols);

    Mat ret(a.rows, a.cols);

    for(int i=0; i < a.rows; i++) {
        for(int j=0; j < a.cols; j++) {
            ret(i,j) = a(i,j)*b(i,j);
        }
    }

    return ret;
}

Mat Mean(const Mat& m)
{
    Mat ret(1, m.cols);

    for(int j=0; j < m.cols; j++) {
        double sum = 0;

        for(int i=0; i < m.rows; i++) {
            sum += m(i,j);
        }

        ret(0,j) = sum / m.cols;
    }

    return ret;
}

Mat Point2Mat(const vector <cv::Point3d>& pts)
{
    Mat ret(3, pts.size());

    for(unsigned int i=0; i < pts.size(); i++) {
        ret(0,i) = pts[i].x;
        ret(1,i) = pts[i].y;
        ret(2,i) = pts[i].z;
    }

    return ret;
}

static Mat RpyMat(const cv::Vec3d& angs)
{
    double cosA = cos(angs[2]);
    double sinA = sin(angs[2]);
    double cosB = cos(angs[1]);
    double sinB = sin(angs[1]);
    double cosC = cos(angs[0]);
    double sinC = sin(angs[0]);

    double cosAsinB = cosA*sinB;
    double sinAsinB = sinA*sinB;

    Mat R(3,3);

    R(0,0) = cosA*cosB;
    R(0,1) = cosAsinB*sinC-sinA*cosC;
    R(0,2) = cosAsinB*cosC+sinA*sinC;

    R(1,0) = sinA*cosB;
    R(1,1) = sinAsinB*sinC+cosA*cosC;
    R(1,2) = sinAsinB*cosC-cosA*sinC;

    R(2,0) = -sinB;
    R(2,1) = cosB*sinC;
    R(2,2) = cosB*cosC;

    return R;
}

static bool RpyAng(const Mat& R, cv::Vec3d& ret) {
    cv::Vec3d angs;

    double R11 = R(0,0);
    double R12 = R(0,1);
    double R13 = R(0,2);

    double R21 = R(1,0);
    double R22 = R(1,1);
    double R23 = R(1,2);

    double R31 = R(2,0);
    double R32 = R(2,1);
    double R33 = R(2,2);

    double sinB = -R31;
    double cosB = sqrt(R11*R11 + R21*R21);

    if(fabs (cosB) > 1e-15) {
        double sinA = R21 / cosB;
        double cosA = R11 / cosB;
        double sinC = R32 / cosB;
        double cosC = R33 / cosB;
        angs[0] = atan2(sinC,cosC);
        angs[1] = atan2(sinB,cosB);
        angs[2] = atan2(sinA,cosA);
    }
    else {
        double sinC = (R12 - R23) / 2;
        double cosC = (R22 + R13) / 2;
        angs[0] = atan2(sinC,cosC);
        angs[1] = M_PI_2;
        angs[2] = 0;

        if(sinB < 0)
            angs = -angs;
    }

    //printf("R\n");
    //Print(R);

    //printf("angs\n");
    //Print(Vec2Mat(angs));

    //printf("angs: %f %f %f\n", angs[0], angs[1], angs[2]);

    Mat a = R-RpyMat(angs);

    //printf("a\n");
    //Print(a);

    //double a = Norm(R-RpyMat(angs));
    //printf("a = %f\n", a);

    if(Norm(R-RpyMat(angs)) > 1e-6) {
        fprintf(stderr, "rpyMat: Error not correct Solution\n");
        return false;
    }

    ret = angs;

    return true;
}

static bool RpyAng_X(const Mat& R, cv::Vec3d& ret) {
    cv::Vec3d ang_zyx;
    bool status = RpyAng(R, ang_zyx);

    if(!status) {
        return false;
    }

    if(fabs(ang_zyx[0]) > M_PI_2) {
         // test the same R
        while ( fabs(ang_zyx[0]) > M_PI_2 ) {
            if(ang_zyx[0] > 0) {
                ang_zyx[0] = ang_zyx[0]+M_PI;
                ang_zyx[1] = 3*M_PI-ang_zyx[1];
                ang_zyx[2] = ang_zyx[2]+M_PI;

                ang_zyx[0] -= 2*M_PI;
                ang_zyx[1] -= 2*M_PI;
                ang_zyx[2] -= 2*M_PI;
            }
            else {
              ang_zyx[0] = ang_zyx[0]+M_PI;
              ang_zyx[1] = 3*M_PI-ang_zyx[1];
              ang_zyx[2] = ang_zyx[2]+M_PI;
            }
        }
    }

    ret = ang_zyx;

    return true;
}

static Mat Sq(const Mat& m)
{
    Mat ret(m.rows, m.cols);

    for(int i=0; i < m.rows; i++) {
        for(int j=0; j < m.cols; j++) {
            ret(i,j) = m(i,j)*m(i,j);
        }
    }

    return ret;
}

static Mat Sum(const Mat& m, int dim=1) {
    // columns
    if(dim == 1) {
        Mat ret(1, m.cols);

        for(int j=0; j < m.cols; j++) {
            double sum = 0;

            for(int i=0; i < m.rows; i++) {
                sum += m(i,j);
            }

            ret(0,j) = sum;
        }

        return ret;
    }
    else {
        Mat ret(m.rows, 1);

        for(int i=0; i < m.rows; i++) {
            double sum = 0;

            for(int j=0; j < m.cols; j++) {
                sum += m(i,j);
            }

            ret(i,0) = sum;
        }

        return ret;
    }
}


static Mat GetRotationbyVector(const cv::Vec3d& v1, const cv::Vec3d& v2)
{
    //printf("GetRotationbyVector\n");

    double winkel = acos(v2.dot(v1));

    //printf("winkel = %f\n", winkel);

    Quaternion QU = Quaternion_byAngleAndVector(winkel,v2.cross(v1));

    //printf("QU\n");
    //Print(QU);

    Mat R = quat2mat(QU);

    //printf("R\n");
    //Print(R);

    Mat a = Sum(Sq(NormRv(v1) - R*NormRv(v2)));

    //printf("a\n");
    //Print(a);

    double mag = a(0,0)*a(0,0);

    if(mag > 1e-3) {
        fprintf(stderr, "Error in GetRotationbyVector()\n");
        exit(1);
    }

    return R;
}


bool DecomposeR(const Mat& R, Mat& Rz2, Mat& ret) {
    //printf("\nDecomposeR\n");

    double cl = atan2(R(2,1), R(2,0));
    Mat Rz = RpyMat(cv::Vec3d(0,0,cl));
/*
    printf("cl = %f\n", cl);
    printf("Rz\n");
    Print(Rz);
    printf("R\n");
    Print(R);
*/
    Mat R_ = R*Rz;

    //printf("R_\n");
    //Print(R_);

    if(R_(2,1) > 1e-3) {
        fprintf(stderr, "error in DecomposeR 1\n");
        return false;
    }

    cv::Vec3d ang_zyx;
    bool status = RpyAng_X(R_, ang_zyx);

    if(!status) {
        return false;
    }

    if(fabs(ang_zyx[0]) > 1e-3) {
        fprintf(stderr, "error in DecomposeR 2\n");
        return false;
    }

    Rz2 = Rz*RpyMat(cv::Vec3d(0,0,M_PI));
    R_ = R*Rz2;

    if(R_(2,1) > 1e-3) {
        fprintf(stderr, "error in DecomposeR 3\n");
        return false;
    }

    // why do we do this?
    status = RpyAng_X(R_, ang_zyx);

    if(!status) {
        return false;
    }

    ret = Rz;

    return true;
}

static void GetRotationY_wrtT(const Mat& v, const Mat& p, const Mat&, const Mat& Rz, vector <double>& al, Mat& tnew, vector <double>& at) {
    vector <Mat> V(v.cols);
    Mat vv(3,1);

    for(int i=0; i < v.cols; i++) {
        vv(0,0) = v(0,i);
        vv(1,0) = v(1,i);
        vv(2,0) = v(2,i);

        Mat tmp = vv.t()*vv;
        double a = tmp(0,0);
        V[i] = (vv*vv.t()) / a;
    }

    //generate G
    Mat G = Mat::zeros(3,3);

    for(int i=0; i < v.cols; i++) {
       G += V[i];
    }

    Mat _eye = Mat::eye(3,3);

    G = (_eye - G/v.cols).inv()/v.cols;

    //printf("G\n");
    //Print(G);

    // generate opt_t*[bt^2 bt 1]

    Mat opt_t = Mat::zeros(3,3);

    for(int i=0; i < v.cols; i++) {
        double v11 = V[i](0,0);
        double v12 = V[i](0,1);
        double v13 = V[i](0,2);
        double v21 = V[i](1,0);
        double v22 = V[i](1,1);
        double v23 = V[i](1,2);
        double v31 = V[i](2,0);
        double v32 = V[i](2,1);
        double v33 = V[i](2,2);

        double px = p(0,i);
        double py = p(1,i);
        double pz = p(2,i);

        double r1 = Rz(0,0);
        double r2 = Rz(0,1);
        double r3 = Rz(0,2);

        double r4 = Rz(1,0);
        double r5 = Rz(1,1);
        double r6 = Rz(1,2);

        double r7 = Rz(2,0);
        double r8 = Rz(2,1);
        double r9 = Rz(2,2);

        opt_t(0,0) += (((v11-1)*r2+v12*r5+v13*r8)*py+(-(v11-1)*r1-v12*r4-v13*r7)*px+(-(v11-1)*r3-v12*r6-v13*r9)*pz);
        opt_t(0,1) += ((2*(v11-1)*r1+2*v12*r4+2*v13*r7)*pz+(-2*(v11-1)*r3-2*v12*r6-2*v13*r9)*px);
        opt_t(0,2) += ((v11-1)*r1+v12*r4+v13*r7)*px+((v11-1)*r3+v12*r6+v13*r9)*pz+((v11-1)*r2+v12*r5+v13*r8)*py;

        opt_t(1,0) += ((v21*r2+(v22-1)*r5+v23*r8)*py+(-v21*r1-(v22-1)*r4-v23*r7)*px+(-v21*r3-(v22-1)*r6-v23*r9)*pz);
        opt_t(1,1) += ((2*v21*r1+2*(v22-1)*r4+2*v23*r7)*pz+(-2*v21*r3-2*(v22-1)*r6-2*v23*r9)*px);
        opt_t(1,2) += (v21*r1+(v22-1)*r4+v23*r7)*px+(v21*r3+(v22-1)*r6+v23*r9)*pz+(v21*r2+(v22-1)*r5+v23*r8)*py;

        opt_t(2,0) += ((v31*r2+v32*r5+(v33-1)*r8)*py+(-v31*r1-v32*r4-(v33-1)*r7)*px+(-v31*r3-v32*r6-(v33-1)*r9)*pz);
        opt_t(2,1) += ((2*v31*r1+2*v32*r4+2*(v33-1)*r7)*pz+(-2*v31*r3-2*v32*r6-2*(v33-1)*r9)*px);
        opt_t(2,2) += (v31*r1+v32*r4+(v33-1)*r7)*px+(v31*r3+v32*r6+(v33-1)*r9)*pz+(v31*r2+v32*r5+(v33-1)*r8)*py;
    }

    opt_t = G*opt_t;

    Mat E_2 = Mat::zeros(1,5);


    // estimate Error function E
    for(int i=0; i < v.cols; i++) {
        double px = p(0,i);
        double py = p(1,i);
        double pz = p(2,i);

        Mat Rpi(3,3);

        Rpi(0,0) = -px;
        Rpi(0,1) = 2*pz;
        Rpi(0,2) = px;

        Rpi(1,0) = py;
        Rpi(1,1) = 0;
        Rpi(1,2) = py;

        Rpi(2,0) = -pz;
        Rpi(2,1) = -2*px;
        Rpi(2,2) = pz;

        Mat E = (_eye - V[i])*(Rz*Rpi + opt_t);

        Mat e0(3,1);
        Mat e1(3,1);
        Mat e2(3,1);

        e0(0,0) = E(0,2);
        e0(1,0) = E(1,2);
        e0(2,0) = E(2,2);

        e1(0,0) = E(0,1);
        e1(1,0) = E(1,1);
        e1(2,0) = E(2,1);

        e2(0,0) = E(0,0);
        e2(1,0) = E(1,0);
        e2(2,0) = E(2,0);

/*
        printf("E\n");
        Print(E);

        printf("e2\n");
        Print(e2);
*/
        Mat sum1 = Sum(Sq(e2));
             //printf("e2\n");
        //Print(e2);

        Mat sum2 = Sum(2*Mul(e1,e2));

        //printf("e2\n");
        //Print(e2);

        Mat sum3 = Sum(2*Mul(e0,e2) + Sq(e1));

        //printf("e2\n");
        //Print(e2);

        Mat sum4 = Sum(2*Mul(e0,e1));
        Mat sum5 = Sum(Sq(e0));




        // E_2 =E_2+ sum([e2.^2 2.*e1.*e2 (2.*e0.*e2+e1.^2) 2.*e0.*e1 e0.^2]);
        E_2(0,0) += sum1(0,0);
        E_2(0,1) += sum2(0,0);
        E_2(0,2) += sum3(0,0);
        E_2(0,3) += sum4(0,0);
        E_2(0,4) += sum5(0,0);
    }

    double e4=E_2(0,0);
    double e3=E_2(0,1);
    double e2=E_2(0,2);
    double e1=E_2(0,3);
    double e0=E_2(0,4);

    //printf("e0 to e4 = %f %f %f %f %f\n", e0, e1, e2, e3, e4);

    double a4=-e3;
    double a3=(4*e4-2*e2);
    double a2=(-3*e1+3*e3);
    double a1=(-4*e0+2*e2);
    double a0=e1;

    double coeffs[5];
//    double z[10];
/*
    // backwards in GSL
    coeffs[0] = a0;
    coeffs[1] = a1;
    coeffs[2] = a2;
    coeffs[3] = a3;
    coeffs[4] = a4;
*/
    coeffs[0] = a4;
    coeffs[1] = a3;
    coeffs[2] = a2;
    coeffs[3] = a1;
    coeffs[4] = a0;

    //printf("coeffs = %f %f %f %f\n", coeffs[0], coeffs[1], coeffs[2], coeffs[3]);
    int degrees = 4;
    double zero_real[5];
    double zero_imag[5];

    memset(zero_real, 0, sizeof(double)*5);
    memset(zero_imag, 0, sizeof(double)*5);

    rpoly(coeffs, &degrees, zero_real, zero_imag);

/*
    gsl_poly_complex_workspace *w = gsl_poly_complex_workspace_alloc(5);
    gsl_poly_complex_solve (coeffs, 5, w, z);
    gsl_poly_complex_workspace_free (w);
*/
    // get all valid solutions -> which are real zero

    at.clear();

    // Nghia - modified a bit here, if it fails use the original
    for(int i=0; i < 5; i++) {
        double _at = zero_real[i];

        double p1 = pow(1.0 + _at*_at, 3.0);

        if(fabs(p1) > 0.1 && zero_imag[i] == 0) {
            at.push_back(_at);
        }
    }

/*
    // check for valid solutions
    for(int i=0; i < 8; i+=2) {
        double _at = z[i];

        double p1 = pow(1.0 + _at*_at, 3.0);

        if(fabs(p1) > 0.1 && z[i+1] == 0)
            at.push_back(_at);
    }
*/
    //printf("at size: %d\n", at.size());

    al.resize(at.size());

    vector <double> al2, at2;

    for(unsigned int i=0; i < at.size(); i++) {
        double sa = (2.f*at[i]) / (1.f +at[i]*at[i]);
        double ca = (1.f - at[i]*at[i]) / (1.f + at[i]*at[i]);

        al[i] = atan2(sa,ca) * 180/M_PI;

        double tMaxMin = (4*a4*at[i]*at[i]*at[i] + 3*a3*at[i]*at[i] + 2*a2*at[i] + a1);

        if(tMaxMin > 0) {
            al2.push_back(al[i]);
            at2.push_back(at[i]);
        }
    }

    al = al2;
    at = at2;

    tnew = Mat(3,al.size());

    for(unsigned int a=0; a < al.size(); a++) {
        Mat R = Rz*RpyMat(cv::Vec3d(0, (al[a]*M_PI/180), 0));
        Mat t_opt = Mat::zeros(3,1);

        Mat pcol(3,1);

        for(int i=0; i < v.cols; i++) {
            pcol(0,0) = p(0,i);
            pcol(1,0) = p(1,i);
            pcol(2,0) = p(2,i);

            t_opt = t_opt + (V[i] - _eye)*R*pcol;
        }

        t_opt = G*t_opt;

        tnew(0,a) = t_opt(0,0);
        tnew(1,a) = t_opt(1,0);
        tnew(2,a) = t_opt(2,0);
    }
}

static bool GetRfor2ndPose_V_Exact(const Mat& v, const Mat& P, const Mat& R, const Mat& t, vector<Solution>& ret) {
    //printf("\nGetRfor2ndPose_V_Exact\n");
    //printf("R\n");
    //Print(R);

    Mat Rz2;
    Mat RzN;

    bool status = DecomposeR(R, Rz2, RzN);

    if(!status) {
        return false;
    }

    Mat R_ = R*RzN;

    Mat P_ = RzN.t()*P;

    cv::Vec3d ang_zyx;

    status = RpyAng_X(R_, ang_zyx);

    if(!status) {
        return false;
    }

    Mat Ry = RpyMat(cv::Vec3d(0,ang_zyx[1],0));
    Mat Rz = RpyMat(cv::Vec3d(0,0,ang_zyx[2]));

    vector <double> bl;
    Mat Tnew;
    vector <double> at;

    GetRotationY_wrtT(v ,P_,t,Rz, bl, Tnew, at);

    // Suggestion by csantos
    if(bl.empty()) {
        return false;
    }

    for(unsigned int i=0; i < bl.size(); i++) {
        bl[i] = bl[i]/180*M_PI;
    }

    // we got 2 solutions. YEAH
    vector <Mat> V(v.cols);

    Mat tmp(3,1);

    for(int i=0; i < v.cols; i++) {
        tmp(0,0) = v(0,i);
        tmp(1,0) = v(1,i);
        tmp(2,0) = v(2,i);

        Mat a = tmp.t()*tmp;

        V[i] = tmp*tmp.t() / a(0,0);
    }

    vector <Solution> sol(bl.size());

    for(unsigned int j=0; j < bl.size(); j++) {
        sol[j].bl = bl[j];
        sol[j].at = at[j];

        Ry = RpyMat(cv::Vec3d(0,bl[j],0));
        sol[j].R = Rz*Ry*RzN.t();

        //printf("sol[j].R\n");
        //Print(sol[j].R);


        sol[j].t(0,0) = Tnew(0,j);
        sol[j].t(1,0) = Tnew(1,j);
        sol[j].t(2,0) = Tnew(2,j);

        // test the Error
        double E=0;
        Mat _eye = Mat::eye(3,3);
        Mat Pcol(3,1);

        for(int i=0; i < v.cols; i++) {
            Pcol(0,0) = P(0,i);
            Pcol(1,0) = P(1,i);
            Pcol(2,0) = P(2,i);

            Mat a = Sum(Sq((_eye - V[i])*(sol[j].R*Pcol + sol[j].t)));

            E = E + a(0,0);
        }

        sol[j].E = E;
    }

    ret = sol;

    return true;
}

static bool Get2ndPose_Exact(const Mat& v, const Mat& P, const Mat& R, const Mat& t, vector<Solution>& ret) {
    //printf("Get2ndPose_Exact\n");

    Mat cent = NormRv(Mean(NormRv(v).t()).t());
    cv::Vec3d _cent;

    _cent[0] = cent(0,0);
    _cent[1] = cent(1,0);
    _cent[2] = cent(2,0);

    Mat Rim = GetRotationbyVector(cv::Vec3d(0,0,1), _cent);

    Mat v_ = Rim*v;
    cent = NormRv(Mean(NormRv(v_).t()).t());

    Mat R_ = Rim*R;
    Mat t_ = Rim*t;
/*
    printf("cent\n");
    Print(cent);

    printf("Rim\n");
    Print(Rim);

    printf("R\n");
    Print(R);

    printf("t\n");
    Print(t);

    printf("R_\n");
    Print(R_);

    printf("t_\n");
    Print(t_);
*/
    vector<Solution> sol;

    bool status = GetRfor2ndPose_V_Exact(v_,P,R_,t_, sol);

    if(!status) {
        return false;
    }

    // de Normalise the Pose
    for(unsigned int i=0; i < sol.size(); i++) {
        //printf("BEFORE\n");
        //Print(sol[i].R);

        sol[i].R = Rim.t()*sol[i].R;

        //printf("AFTER\n");
        //Print(sol[i].R);
        sol[i].t = Rim.t()*sol[i].t;
    }

    ret = sol;

    return true;
}




static Mat Xform(const Mat& P, const Mat& R, const Mat& t)
{
    Mat ret(3,P.cols);

    for(int i=0; i < P.cols; i++) {
        double x = P(0,i);
        double y = P(1,i);
        double z = P(2,i);

        ret(0,i) = R(0,0)*x + R(0,1)*y + R(0,2)*z + t(0,0);
        ret(1,i) = R(1,0)*x + R(1,1)*y + R(1,2)*z + t(1,0);
        ret(2,i) = R(2,0)*x + R(2,1)*y + R(2,2)*z + t(2,0);
    }

    return ret;
}

static void AbsKernel(Mat P, Mat Q, const vector <Mat>& F, const Mat& G,
               Mat& R, Mat& t, Mat& Qout, double& err2)
{
    int n = P.cols;

    Mat QQ(3,1);

    for(int i=0; i < n; i++) {
        QQ(0,0) = Q(0,i);
        QQ(1,0) = Q(1,i);
        QQ(2,0) = Q(2,i);

        QQ= F[i]*QQ;

        Q(0,i) = QQ(0,0);
        Q(1,i) = QQ(1,0);
        Q(2,i) = QQ(2,0);
    }

    Mat pbar = Sum(P,2)/n;
    Mat qbar = Sum(Q,2)/n;

    // compute P' and Q'
    for(int i=0; i < n; i++) {
        P(0,i) -= pbar(0,0);
        P(1,i) -= pbar(1,0);
        P(2,i) -= pbar(2,0);
    }

    // use SVD solution
    // compute M matrix
    Mat M = Mat::zeros(3,3);
    Mat PP(3,1);

    for(int i=0; i < n; i++) {
        PP(0,0) = P(0,i);
        PP(1,0) = P(1,i);
        PP(2,0) = P(2,i);

        QQ(0,0) = Q(0,i);
        QQ(1,0) = Q(1,i);
        QQ(2,0) = Q(2,i);

        M += PP*QQ.t();
    }

    cv::SVD decomp(M);

    R = decomp.vt.t()*(decomp.u.t());
    Mat v = decomp.vt.t();

    if(sign(determinant(R)) == 1) {
        t = EstimateT(R,G,F,P);

        if(t(2,0) < 0) {
            // we need to invert the t
            // negate the 3rd column

            v(0,2) = -v(0,2);
            v(1,2) = -v(1,2);
            v(2,2) = -v(2,2);

            // we need to invert the t
            R = -v*decomp.u.t();
            t = EstimateT(R,G,F,P);
        }
    }
    else {
        // negate the 3rd column
        v(0,2) = -v(0,2);
        v(1,2) = -v(1,2);
        v(2,2) = -v(2,2);

        R = v*decomp.u.t();
        t = EstimateT(R,G,F,P);

        if(t(2,0) < 0) {
            R = -v*(decomp.u.t());
            t = EstimateT(R,G,F,P);
        }
    }


    // calculate error
    Mat _eye = Mat::eye(3,3);
    Mat vec(3,1);

    err2 = 0;
    Qout = Xform(P, R, t);

    for(int i=0; i < n; i++) {
        QQ(0,0) = Qout(0,i);
        QQ(1,0) = Qout(1,i);
        QQ(2,0) = Qout(2,i);

        vec = (_eye - F[i])*QQ;

        double x = vec(0,0);
        double y = vec(1,0);
        double z = vec(2,0);

        err2 += (x*x + y*y + z*z);
    }
}

static void ObjPose(const Mat _P, Mat Qp, Mat initR,
             Mat& R, Mat& t, int& it, double& obj_err, double& img_err)
{
    Mat P = _P.clone(); // make a copy, else _P will get modified, nasty GOTCHA!

    int n = P.cols;
    it = 0;

    Mat pbar = Sum(P,2) / n;

    // move the origin to the center of P
    for(int i=0; i < n; i++) {
        P(0,i) -= pbar(0,0);
        P(1,i) -= pbar(1,0);
        P(2,i) -= pbar(2,0);
    }

    // compute projection matrices
    vector <Mat> F(n);
    Mat V(3,1);
    Mat ret(1,1);

    for(int i=0; i < n; i++) {
        F[i].create(3,3);

        V(0,0) = Qp(0,i);
        V(1,0) = Qp(1,i);
        V(2,0) = Qp(2,i);

        ret = V.t()*V;

        F[i] = (V*V.t()) / ret(0,0);
    }

    // compute the matrix factor required to compute t
    Mat sumF = Mat::zeros(3,3);

    for(int i=0; i < n; i++) {
        sumF += F[i];
    }

    Mat tFactor = (Mat::eye(3,3)-sumF/n).inv()/n;

    double old_err;
    Mat Qi;
    Mat Ri, ti;

    if(initR.data) {
        Ri = initR;

        Mat sum_ = Mat::zeros(3,1);

        Mat _eye = Mat::eye(3,3);
        Mat PP(3,1);

        for(int i=0; i < n; i++) {
            PP(0,0) = P(0,i);
            PP(1,0) = P(1,i);
            PP(2,0) = P(2,i);

            sum_ = sum_ + (F[i] - _eye)*Ri*PP;
        }

        ti = tFactor*sum_;

        // calculate error
        Qi = Xform(P, Ri, ti);

        old_err = 0;

        Mat vec(3,1);
        Mat QiCol(3,1);

        for(int i=0; i < n; i++) {
            QiCol(0,0) = Qi(0,i);
            QiCol(1,0) = Qi(1,i);
            QiCol(2,0) = Qi(2,i);

            vec = (_eye - F[i])*QiCol;

            double x = vec(0,0);
            double y = vec(1,0);
            double z = vec(2,0);

            old_err += (x*x + y*y + z*z);
        }
    }
    else {
        // no initial guess; use weak-perspective approximation
        AbsKernel(P, Qp, F, tFactor, Ri, ti, Qi, old_err);
        it = 1;

        //printf("SVD\n");
        //Print(Ri);
    }

    // compute next pose estimate
    double new_err;

    AbsKernel(P, Qi, F, tFactor, Ri, ti, Qi, new_err);

    it = it + 1;

    //printf("before while\n");

    while(fabs((old_err-new_err)/old_err) > TOL && (new_err > EPSILON)) {
        old_err = new_err;
        // compute the optimal estimate of R
        AbsKernel(P, Qi, F, tFactor, Ri, ti, Qi, new_err);

        //printf("it %d\n", it);
        it = it + 1;
    }

    R = Ri;
    t = ti;
    obj_err = sqrt(new_err/n);

    // calculate image-space error
    Mat Pcol(3,1);
    Mat Qproj;

    img_err = 0;

    for(int i=0; i < n; i++) {
        Pcol(0,0) = P(0,i);
        Pcol(1,0) = P(1,i);
        Pcol(2,0) = P(2,i);

        Qproj = Ri*Pcol + ti;

        double xx = (Qproj(0,0)/Qproj(2,0)) - Qp(0,0);
        double yy = (Qproj(1,0)/Qproj(2,0)) - Qp(1,0);

        img_err += (xx*xx + yy*yy);
    }

    img_err = sqrt(img_err/n);

    // get back to original refernce frame

    t = t - Ri*pbar;
}

bool Rpp(const Mat& model_3D, const Mat& iprts, Mat& Rlu, Mat& tlu, int& it1, double& obj_err1, double& img_err1) {
    if(Rlu.data) ObjPose(model_3D, iprts, Rlu, Rlu, tlu, it1, obj_err1, img_err1);
    else ObjPose(model_3D, iprts, Mat(), Rlu, tlu, it1, obj_err1, img_err1);

    // get 2nd Pose
    vector <Solution> sol;
    bool status = Get2ndPose_Exact(iprts, model_3D, Rlu, tlu, sol);
    assert_(status);

    //refine with lu
    int bestIdx=-1;
    double lowestErr = 1e6;

    for(unsigned int i=0; i < sol.size(); i++) {
        ObjPose(model_3D, iprts, sol[i].R, Rlu, sol[i].t, it1, obj_err1, img_err1);

        sol[i].R = Rlu;
        sol[i].obj_err = obj_err1;
        sol[i].img_err = img_err1;

        if(obj_err1 < lowestErr) {
            lowestErr = obj_err1;
            bestIdx = i;
        }
    }

    Rlu = sol[bestIdx].R;
    tlu = sol[bestIdx].t;
    obj_err1 = sol[bestIdx].obj_err;
    img_err1 = sol[bestIdx].img_err;

    return true;
}

bool Rpp(const ref<vec2>& modelQ, const ref<vec2>& imageQ, mat3& R, vec3& t, int& i, double& objε, double& imgε) {
    Mat model (3, modelQ.size);
    Mat image (3, imageQ.size);
    for(int i: range(modelQ.size)) {
        model(0,i) = modelQ[i].x;
        model(1,i) = modelQ[i].y;
        model(2,i) = 0;
        image(0,i) = imageQ[i].x;
        image(1,i) = imageQ[i].y;
        image(2,i) = 1;
    }
    Mat Rlu, tlu;
    Rpp(model, image, Rlu, tlu, i, objε, imgε);
    assert_(Rlu.rows == 3 && Rlu.cols == 3);
    for(int i: range(3)) for(int j: range(3)) R(i, j) = Rlu(i, j);
    assert_(tlu.rows == 3 && tlu.cols == 1);
    for(int i: range(3)) t[i] = tlu(i, 0);
    return true;
}
