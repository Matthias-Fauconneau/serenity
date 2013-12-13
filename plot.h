#pragma once
#include "widget.h"
#include "display.h"

struct Plot : Widget {
    default_move(Plot);

    real minY=inf, meanY=0, maxY=0;
    buffer<real> Y;
    Plot(ref<real> data) : Y(data.size) {
        assert_(data.size%Y.size==0);
        real sumY = 0;
        for(int x: range(Y.size)) {
            real sum=0; for(uint n: range(x*data.size/Y.size,(x+1)*data.size/Y.size)) sum += data[n];
            real y = sum / (data.size/Y.size);
            Y[x] = y;
            sumY += y;
            maxY = max(maxY, y);
            minY = min(minY, y);
        }
        meanY = sumY / Y.size;
        for(int x: range(Y.size)) Y[x] = Y[x] / maxY; // Linear plot
        //for(int x: range(Y.size)) Y[x] = (Y[x]-minY) / (maxY-minY); // Affine plot
        //for(int x: range(Y.size)) Y[x] = (log2(Y[x])-log2(minY)) / (log2(maxY)-log2(minY)); // Log plot (min-max
        //for(int x: range(Y.size)) Y[x] = (log2(Y[x])-log2(meanY)) / (log2(maxY)-log2(meanY)); // Log plot (mean-max)
    }
    void render(int2 position, int2 size) {
        for(uint x: range(Y.size)) line(position+vec2(x * size.x / Y .size, (1-Y[x])*size.y),position+vec2((x+1) * size.x / Y .size, (1-Y[x+1])*size.y));
    }
};
