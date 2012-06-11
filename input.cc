#include "input.h"
#include "poll.h"
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

Input::Input(const char* path) {
    fd = open(path, O_RDONLY|O_NONBLOCK);
    assert(fd>0);
    registerPoll({fd, POLLIN});
}

Input::~Input() { close(fd); }

void Input::event(pollfd) {
    input_event e;
    while(read(fd, &e, sizeof(e)) > 0) {
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
        } else if (e.type == EV_ABS) {
            int i = e.code;
            int v = e.value;
#if CALIBRATE
            // calibrate without a priori
            const int M=1<<10;
            static int min[3]={M,M,M};
            static int max[3]={-M,-M,-M};
#else
            // calibration sampled on my touchbook
            static int min[3]={337,-80,352};
            static int max[3]={506,63,486};
#endif
            if(v<=min[i]) min[i]=v-1;
            if(v>=max[i]) max[i]=v+1;
            float f = (2*float(v-min[i])/float(max[i]-min[i]))-1; //automatic calibration to [-1,1]
            if(i==0) x=f; else if(i==1) y=f; else z=f;
        } else if(e.type == EV_KEY) { //button
            signal<>* shortcut = keyPress.find(e.code);
            log(e.code,shortcut?"!":"");
            if(shortcut) shortcut->emit();
        }
    }
}
