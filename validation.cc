#include "validation.h"
#include "time.h"

array<Ball> randomBalls(Volume16& target, int minimalDistance, int maximumRadius) {
    const int X=target.x, Y=target.y, Z=target.z, XY = X*Y;
    Random random;

    array<Ball> balls;
    while(balls.size<(uint)min(X,min(Y, Z))/8) {
        uint radius = 1+random%(min(maximumRadius, min(X-1, min(Y-1, Z-1))/2-minimalDistance)-1);
        uint margin = minimalDistance+radius;
        Ball a = {int3(margin+random%(X-2*margin), margin+random%(Y-2*margin), margin+random%(Z-2*margin)), radius}; // Random candidate ball
        assert(a.position>=int3(radius) && a.position<int3(X,Y,Z)-int3(radius));
        for(Ball b: balls) if(sqr(a.radius+minimalDistance+b.radius)>sqr(a.position-b.position)) goto break_; // Discards candidates intersecting any other ball
        /*else*/ balls << a;
        break_: ;
    }
    clear<uint16>(target, target.size(), target.maximum); // Sets target to maximum value
    parallel(balls.size, [&](uint, uint i) {
        Ball ball=balls[i]; // Rasterizes all balls to volume
        uint16* const targetData = target + ball.position.z*XY + ball.position.y*X + ball.position.x;
        int radius = ball.radius, sqRadius = radius*radius;
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
    return balls;
}
