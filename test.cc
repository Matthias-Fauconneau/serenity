#include "thread.h"
#include "simd.h"
#include "vector.h"
#include "mwc.h"

struct Test {
    Test() {
        const vec3 N (1,  0, -0);
        const vec3 v0 (552.8, -64.57, -0);
        const vec3 v1 (0, -64.57, -0);
        const vec3 v2 (0, -64.57, 559.2);
        const float abc = dot(v0, cross(v1, v2));
        const float a = length(v0);
        const float b = length(v1);
        const float c = length(v2);
        const float den = a*b*c + dot(v0,v1)*c + dot(v0,v2)*b + dot(v1,v2)*a;
        const float O = -2*atan(abc, den); // Solid angle (0-4PI)
        //const float cosa = dot(v1-v0, v2-v0)/(length(v1-v0)*length(v2-v0));
        //const float cosb = dot(v0-v1, v2-v1)/(length(v0-v1)*length(v2-v1));
        //const float cosc = dot(v0-v2, v1-v2)/(length(v0-v2)*length(v1-v2));
        const vec3 Na  = cross(v1, v2);
        const vec3 Nb  = cross(v2, v0);
        const vec3 Nc  = cross(v0, v1);
        const float cosa = dot(Nb, Nc)/(length(Nb)*length(Nc));
        const float cosb = dot(Nc, Na)/(length(Nc)*length(Na));
        const float cosc = dot(Na, Nb)/(length(Na)*length(Nb));
        const float sina = sqrt(1-sq(cosa));
        const float alpha = acos(cosa);
        const float beta = acos(cosb);
        const float gamma = acos(cosc);
        assert_(O == alpha+beta+gamma-PI, O, alpha+beta+gamma-PI);
        assert_(alpha >= 0 && alpha <= PI);
        const vec3 oa = v0/a;
        const vec3 ob = v1/b;
        const vec3 oc = v2/c;
        const vec3 Q = normalize(oc-dot(oa,oc)*oa);
        Random random;
        const v8sf A = random() * O; // New area
        const Vec<v8sf, 2> st = cossin(A-alpha);
        const v8sf t = st._[0];
        const v8sf s = st._[1];
        const v8sf u = t - cosa;
        const v8sf v = s + sina * cosc;
        const v8sf q = ((v*t-u*s)*cosa-v) / ((v*s+u*t)*sina); // cos b'
        const v8sf q1 = sqrt(1-q*q);
        for(int k: range(8)) assert_(q[k] >= -1 && q[k]<=1 && q[k]>=cosb, cosb, q[k]);
        const v8sf Cx = q*oa.x + q1*Q.x;
        const v8sf Cy = q*oa.y + q1*Q.y;
        const v8sf Cz = q*oa.z + q1*Q.z;
        const v8sf dotBC = ob.x*Cx + ob.y*Cy + ob.z*Cz;
        const v8sf z = 1 - random() * (1 - dotBC);
        for(int k: range(8)) assert_(z[k] >= -1 && z[k]<=1, z[k], O, abc/(a*b*c), den);
        for(int k: range(8)) assert_((cosc <= z[k] && z[k] <= cosa) || (cosa <= z[k] && z[k] <= cosc), cosa, cosc, z[k]);
        const v8sf Zx = Cx - dotBC*ob.x;
        const v8sf Zy = Cy - dotBC*ob.y;
        const v8sf Zz = Cz - dotBC*ob.z;
        const v8sf z1 = sqrt( (1-z*z) / (Zx*Zx+Zy*Zy+Zz*Zz) );
        const v8sf Lx = z*ob.x + z1*Zx;
        const v8sf Ly = z*ob.y + z1*Zy;
        const v8sf Lz = z*ob.z + z1*Zz;
        const v8sf dotNL = N.x*Lx + N.y*Ly + N.z*Lz; // FIXME
        for(int k: range(8)) {
            vec3 L(Lx[k], Ly[k], Lz[k]);
            assert_(dotNL[k]>-0.005, "A", oa, ob, oc, dot(oa, L), dot(ob, L), dot(oc, L));
        }
    }
} app;
