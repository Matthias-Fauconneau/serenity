#include "window.h"
#include "text.h"
#include "slice.h"

//#include "synthetic.h" // Links synthetic function
Volume8 synthetic(int3 size);

struct Tomography {
    SliceView slice = ::synthetic(128);
    Window window {&slice, int2(512), "Tomography"_};
    Tomography() {
        window.actions[Escape] = []{ exit(); };
        window.background = Window::NoBackground;
        window.show();
    }
} tomography;
