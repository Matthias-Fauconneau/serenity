#include "ComplexIor.h"
#include "ComplexIorData.h"
// http://homepages.rpi.edu/~schubert/Educational-resources/Materials-Refractive-index-and-extinction-coefficient.pdf

bool lookup(const std::string &name, Vec3f &eta, Vec3f &k)
{
    for (int i = 0; i < ComplexIorCount; ++i) {
        if (complexIorList[i].name == name) {
            eta = complexIorList[i].eta;
            k   = complexIorList[i].k;
            return true;
        }
    }
    return false;
}
