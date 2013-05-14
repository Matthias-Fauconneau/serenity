#include "primitive.h"
#include "time.h"

array<Primitive> randomPrimitives(int X, int Y, int Z, int minimalDistance, int maximumRadius) {
    Random random;
    array<Primitive> primitives;
    while(primitives.size<(uint)min(X,min(Y, Z))/8) { // Produces only spheres, FIXME: cylinder, cone
        uint radius = 1+random%(min(maximumRadius, min(X-1, min(Y-1, Z-1))/2-minimalDistance)-1);
        uint margin = minimalDistance+radius;
        int3 position = int3(margin+random%(X-2*margin), margin+random%(Y-2*margin), margin+random%(Z-2*margin));
        assert(position>=int3(radius) && position<int3(X,Y,Z)-int3(radius));
        Primitive a {Primitive::Sphere, vec3(position), (float)radius}; // Random candidate primitive
        for(Primitive b: primitives) { // Discards candidates intersecting any other primitive
            assert(a.type==Primitive::Sphere && b.type==Primitive::Sphere); //FIXME: cylinder, cone
            if(sqr(a.radius+minimalDistance+b.radius)>sqr(a.position-b.position)) goto break_;
        }
        /*else*/ primitives << a;
        break_: ;
    }
    return primitives;
}

void rasterize(Volume16& target, const array<Primitive>& primitives) {
    const int X=target.x, Y=target.y, XY = X*Y;
    clear<uint16>(target, target.size(), target.maximum); // Sets target to maximum value
    parallel(primitives.size, [&](uint, uint i) {
        Primitive p=primitives[i]; // Rasterizes all primitives to volume
        assert(p.type==Primitive::Sphere); //FIXME: cylinder, cone
        uint16* const targetData = target + int(p.position.z)*XY + int(p.position.y)*X + int(p.position.x);
        int radius = p.radius, sqRadius = radius*radius;
        for(int dz=-radius; dz<=radius; dz++) {
            uint16* const targetZ= targetData + dz*XY;
            for(int dy=-radius; dy<=radius; dy++) {
                uint16* const targetZY= targetZ + dy*X;
                for(int dx=-radius; dx<=radius; dx++) {
                    uint16* const targetZYX= targetZY + dx;
                    if(dx*dx+dy*dy+dz*dz<=sqRadius) targetZYX[0] = 0;
                }
            }
        }
    } );
}
