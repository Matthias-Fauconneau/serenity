#pragma once
#include "widget.h"
#include "display.h"

struct Plot : Widget {
    ref<real> X, Y;
    real minX=-1, maxX=1, minY=-1, maxY=1;
    Plot(ref<real> X, ref<real> Y, real maxY) : X(X), Y(Y), minY(-maxY), maxY(maxY) { assert(X.size==Y.size); }
    void render(int2 position, int2 size) {
        {float y0 = (0-minY)/(maxY-minY)*size.y; line(position+int2(0,y0), position+int2(size.x,y0), 1./2);}
        for(uint i: range(X.size-1)) line(
                    vec2(position)+vec2((X[i+0]-minX)/(maxX-minX)*size.x, (maxY-Y[i+0])/(maxY-minY)*size.y),
                    vec2(position)+vec2((X[i+1]-minX)/(maxX-minX)*size.x, (maxY-Y[i+1])/(maxY-minY)*size.y) );
    }
};
