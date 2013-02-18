///From "Fast Ray / Axis-Aligned Bounding Box Overlap Tests using Ray Slopes"
#include "vector.h"

enum {MMM, MMP, MPM, MPP, PMM, PMP, PPM, PPP, POO, MOO, OPO, OMO, OOP, OOM, OMM,OMP,OPM,OPP,MOM,MOP,POM,POP,MMO,MPO,PMO,PPO};
struct Ray {
	vec3 O, D, iD;
	uint sign;
	float ibyj, ibyk, jbyi, kbyj, jbyk, kbyi; //slope
	float c_xy, c_xz, c_yx, c_yz, c_zx, c_zy;	
    Ray(vec3 O, vec3 D) : O(O), D(D), iD(1/D) {
        //ray slope
        ibyj = D.x * iD.y;
        jbyi = D.y * iD.x;
        jbyk = D.y * iD.z;
        kbyj = D.z * iD.y;
        ibyk = D.x * iD.z;
        kbyi = D.z * iD.x;
        c_xy = O.y - jbyi * O.x;
        c_xz = O.z - kbyi * O.x;
        c_yx = O.x - ibyj * O.y;
        c_yz = O.z - kbyj * O.y;
        c_zx = O.x - ibyk * O.z;
        c_zy = O.y - jbyk * O.z;

        //ray slope sign //FIXME: just write 26 if and let compiler build the tree
        if(D.x < 0) {
            if(D.y < 0) {
                if(D.z < 0) sign = MMM;
                else if(D.z > 0) sign = MMP;
                else sign = MMO;
            } else {
                if(D.z < 0) {
                    sign = MPM;
                    if(D.y==0) sign = MOM;
                } else {
                    if((D.y==0) && (D.z==0)) sign = MOO;
                    else if(D.z==0) sign = MPO;
                    else if(D.y==0) sign = MOP;
                    else sign = MPP;
                }
            }
        } else {
            if(D.y < 0) {
                if(D.z < 0) {
                    sign = PMM;
                    if(D.x==0) sign = OMM;
                } else {
                    if((D.x==0) && (D.z==0)) sign = OMO;
                    else if(D.z==0) sign = PMO;
                    else if(D.x==0) sign = OMP;
                    else sign = PMP;
                }
            } else {
                if(D.z < 0) {
                    if((D.x==0) && (D.y==0)) sign = OOM;
                    else if(D.x==0) sign = OPM;
                    else if(D.y==0) sign = POM;
                    else sign = PPM;
                } else {
                    if(D.x==0) {
                        if(D.y==0) sign = OOP;
                        else if(D.z==0) sign = OPO;
                        else sign = OPP;
                    } else {
                        if((D.y==0) && (D.z==0)) sign = POO;
                        else if(D.y==0) sign = POP;
                        else if(D.z==0) sign = PPO;
                        else sign = PPP;
                    }
                }
            }
        }
    }
    bool intersect(const Ray& r, const vec3& min, const vec3& max, float& t) {
        if(sign==MMM) {
            if((x < min.x) || (y < min.y) || (z < min.z)
                    || (jbyi * min.x - max.y + c_xy > 0)
                    || (ibyj * min.y - max.x + c_yx > 0)
                    || (jbyk * min.z - max.y + c_zy > 0)
                    || (kbyj * min.y - max.z + c_yz > 0)
                    || (kbyi * min.x - max.z + c_xz > 0)
                    || (ibyk * min.z - max.x + c_zx > 0)
                    ) return false;
            t = (max.x - O.x) * iD.x;
            float t1 = (max.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            float t2 = (max.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==MMP) {
            if((x < min.x) || (y < min.y) || (z > max.z)
                    || (jbyi * min.x - max.y + c_xy > 0)
                    || (ibyj * min.y - max.x + c_yx > 0)
                    || (jbyk * max.z - max.y + c_zy > 0)
                    || (kbyj * min.y - min.z + c_yz < 0)
                    || (kbyi * min.x - min.z + c_xz < 0)
                    || (ibyk * max.z - max.x + c_zx > 0)
                    ) return false;
            t = (max.x - O.x) * iD.x;
            float t1 = (max.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            float t2 = (min.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==MPM) {
            if((x < min.x) || (y > max.y) || (z < min.z)
                    || (jbyi * min.x - min.y + c_xy < 0)
                    || (ibyj * max.y - max.x + c_yx > 0)
                    || (jbyk * min.z - min.y + c_zy < 0)
                    || (kbyj * max.y - max.z + c_yz > 0)
                    || (kbyi * min.x - max.z + c_xz > 0)
                    || (ibyk * min.z - max.x + c_zx > 0)
                    ) return false;
            t = (max.x - O.x) * iD.x;
            float t1 = (min.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            float t2 = (max.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==MPP) {
            if ((x < min.x) || (y > max.y) || (z > max.z)
                    || (jbyi * min.x - min.y + c_xy < 0)
                    || (ibyj * max.y - max.x + c_yx > 0)
                    || (jbyk * max.z - min.y + c_zy < 0)
                    || (kbyj * max.y - min.z + c_yz < 0)
                    || (kbyi * min.x - min.z + c_xz < 0)
                    || (ibyk * max.z - max.x + c_zx > 0)
                    ) return false;
            t = (max.x - O.x) * iD.x;
            float t1 = (min.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            float t2 = (min.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==PMM) {
            if ((x > max.x) || (y < min.y) || (z < min.z)
                    || (jbyi * max.x - max.y + c_xy > 0)
                    || (ibyj * min.y - min.x + c_yx < 0)
                    || (jbyk * min.z - max.y + c_zy > 0)
                    || (kbyj * min.y - max.z + c_yz > 0)
                    || (kbyi * max.x - max.z + c_xz > 0)
                    || (ibyk * min.z - min.x + c_zx < 0)
                    ) return false;
            t = (min.x - O.x) * iD.x;
            float t1 = (max.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            float t2 = (max.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==PMP) {
            if ((x > max.x) || (y < min.y) || (z > max.z)
                    || (jbyi * max.x - max.y + c_xy > 0)
                    || (ibyj * min.y - min.x + c_yx < 0)
                    || (jbyk * max.z - max.y + c_zy > 0)
                    || (kbyj * min.y - min.z + c_yz < 0)
                    || (kbyi * max.x - min.z + c_xz < 0)
                    || (ibyk * max.z - min.x + c_zx < 0)
                    ) return false;
            t = (min.x - O.x) * iD.x;
            float t1 = (max.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            float t2 = (min.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==PPM) {
            if ((x > max.x) || (y > max.y) || (z < min.z)
                    || (jbyi * max.x - min.y + c_xy < 0)
                    || (ibyj * max.y - min.x + c_yx < 0)
                    || (jbyk * min.z - min.y + c_zy < 0)
                    || (kbyj * max.y - max.z + c_yz > 0)
                    || (kbyi * max.x - max.z + c_xz > 0)
                    || (ibyk * min.z - min.x + c_zx < 0)
                    ) return false;
            t = (min.x - O.x) * iD.x;
            float t1 = (min.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            float t2 = (max.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==PPP) {
            if ((x > max.x) || (y > max.y) || (z > max.z)
                    || (jbyi * max.x - min.y + c_xy < 0)
                    || (ibyj * max.y - min.x + c_yx < 0)
                    || (jbyk * max.z - min.y + c_zy < 0)
                    || (kbyj * max.y - min.z + c_yz < 0)
                    || (kbyi * max.x - min.z + c_xz < 0)
                    || (ibyk * max.z - min.x + c_zx < 0)
                    ) return false;
            t = (min.x - O.x) * iD.x;
            float t1 = (min.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            float t2 = (min.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==OMM) {
            if((x < min.x) || (x > max.x)
                    || (y < min.y) || (z < min.z)
                    || (jbyk * min.z - max.y + c_zy > 0)
                    || (kbyj * min.y - max.z + c_yz > 0)
                    ) return false;
            t = (max.y - O.y) * iD.y;
            float t2 = (max.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==OMP) {
            if((x < min.x) || (x > max.x)
                    || (y < min.y) || (z > max.z)
                    || (jbyk * max.z - max.y + c_zy > 0)
                    || (kbyj * min.y - min.z + c_yz < 0)
                    ) return false;
            t = (max.y - O.y) * iD.y;
            float t2 = (min.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==OPM) {
            if((x < min.x) || (x > max.x)
                    || (y > max.y) || (z < min.z)
                    || (jbyk * min.z - min.y + c_zy < 0)
                    || (kbyj * max.y - max.z + c_yz > 0)
                    ) return false;
            t = (min.y - O.y) * iD.y;
            float t2 = (max.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==OPP) {
            if((x < min.x) || (x > max.x)
                    || (y > max.y) || (z > max.z)
                    || (jbyk * max.z - min.y + c_zy < 0)
                    || (kbyj * max.y - min.z + c_yz < 0)
                    ) return false;
            t = (min.y - O.y) * iD.y;
            float t2 = (min.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==MOM) {
            if((y < min.y) || (y > max.y)
                    || (x < min.x) || (z < min.z)
                    || (kbyi * min.x - max.z + c_xz > 0)
                    || (ibyk * min.z - max.x + c_zx > 0)
                    ) return false;
            t = (max.x - O.x) * iD.x;
            float t2 = (max.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==MOP) {
            if((y < min.y) || (y > max.y)
                    || (x < min.x) || (z > max.z)
                    || (kbyi * min.x - min.z + c_xz < 0)
                    || (ibyk * max.z - max.x + c_zx > 0)
                    )
                return false;
            t = (max.x - O.x) * iD.x;
            float t2 = (min.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==POM) {
            if((y < min.y) || (y > max.y)
                    || (x > max.x) || (z < min.z)
                    || (kbyi * max.x - max.z + c_xz > 0)
                    || (ibyk * min.z - min.x + c_zx < 0)
                    ) return false;
            t = (min.x - O.x) * iD.x;
            float t2 = (max.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==POP) {
            if((y < min.y) || (y > max.y)
                    || (x > max.x) || (z > max.z)
                    || (kbyi * max.x - min.z + c_xz < 0)
                    || (ibyk * max.z - min.x + c_zx < 0)
                    )
                return false;
            t = (min.x - O.x) * iD.x;
            float t2 = (min.z - O.z) * iD.z;
            if(t2 > t) t = t2;
            return true;
        }
        if(sign==MMO) {
            if((z < min.z) || (z > max.z)
                    || (x < min.x) || (y < min.y)
                    || (jbyi * min.x - max.y + c_xy > 0)
                    || (ibyj * min.y - max.x + c_yx > 0)
                    ) return false;
            t = (max.x - O.x) * iD.x;
            float t1 = (max.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            return true;
        }
        if(sign==MPO) {
            if((z < min.z) || (z > max.z)
                    || (x < min.x) || (y > max.y)
                    || (jbyi * min.x - min.y + c_xy < 0)
                    || (ibyj * max.y - max.x + c_yx > 0)
                    ) return false;
            t = (max.x - O.x) * iD.x;
            float t1 = (min.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            return true;
        }
        if(sign==PMO) {
            if((z < min.z) || (z > max.z)
                    || (x > max.x) || (y < min.y)
                    || (jbyi * max.x - max.y + c_xy > 0)
                    || (ibyj * min.y - min.x + c_yx < 0)
                    ) return false;
            t = (min.x - O.x) * iD.x;
            float t1 = (max.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            return true;
        }
        if(sign==PPO) {
            if((z < min.z) || (z > max.z)
                    || (x > max.x) || (y > max.y)
                    || (jbyi * max.x - min.y + c_xy < 0)
                    || (ibyj * max.y - min.x + c_yx < 0)
                    ) return false;
            t = (min.x - O.x) * iD.x;
            float t1 = (min.y - O.y) * iD.y;
            if(t1 > t) t = t1;
            return true;
        }
        if(sign==MOO) {
            if((x < min.x)
                    || (y < min.y) || (y > max.y)
                    || (z < min.z) || (z > max.z)
                    ) return false;
            t = (max.x - O.x) * iD.x;
            return true;
        }
        if(sign==POO) {
            if((x > max.x)
                    || (y < min.y) || (y > max.y)
                    || (z < min.z) || (z > max.z)
                    ) return false;
            t = (min.x - O.x) * iD.x;
            return true;
        }
        if(sign==OMO) {
            if((y < min.y)
                    || (x < min.x) || (x > max.x)
                    || (z < min.z) || (z > max.z)
                    ) return false;
            t = (max.y - O.y) * iD.y;
            return true;
        }
        if(sign==OPO) {
            if((y > max.y)
                    || (x < min.x) || (x > max.x)
                    || (z < min.z) || (z > max.z)
                    ) return false;
            t = (min.y - O.y) * iD.y;
            return true;
        }
        if(sign==OOM) {
            if((z < min.z)
                    || (x < min.x) || (x > max.x)
                    || (y < min.y) || (y > max.y)
                    ) return false;
            t = (max.z - O.z) * iD.z;
            return true;
        }
        if(sign==OOP) {
            if((z > max.z)
                    || (x < min.x) || (x > max.x)
                    || (y < min.y) || (y > max.y)
                    ) return false;
            t = (min.z - O.z) * iD.z;
            return true;
        }
        return false;
    }
};
