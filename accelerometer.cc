
#include "vector.h"
//parse MMA7455L accelerometer (FIXME: not generic input, should use trigger level)
static float x, y, z; // x = screen Y (+ bottom), Y = screen X (+ right), Z = screen Z (+ behind)
if (e.type == EV_SYN) {
    float roll = atan(sqrt(y*y + z*z) / x);
    float pitch = atan(sqrt(x*x + z*z) / y);
    //const float min = PI/3, max = 2*PI/3;
    const float min = PI/4, max = PI/3;
    static int rotation=0;
    int next=rotation;
    /**/  if(abs(pitch) < min && abs(roll) >= max) { if (pitch > 0) next = 270; else next = 90; }
    else if(abs(roll) < min && abs(pitch) >= max) { if (roll > 0) next= 0; else next = 180; }
    if(next!=rotation) { rotation=next; log(rotation); /*TODO:rotate*/ }
} else
