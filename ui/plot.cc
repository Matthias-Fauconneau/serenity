#include "plot.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "builtin.h"

int2 Plot::sizeHint() { return int2(-1080*4/3/2, -1080/2); }
void Plot::render(int2 position, int2 size) {
    int resolution = ::resolution; ::resolution = 1.5*96; // Scales the size of all text labels
    // Computes axis scales
    vec2 min=0, max=0;
    for(const auto& data: dataSets) for(auto point: data) {
        vec2 p(point.key,point.value);
        min=::min(min,p);
        max=::max(max,p);
    }
    if(!logx && min.x>0) min.x = 0;
    //if(min.y<0) { assert(!logy); min.y=-(max.y=::max(-min.y,max.y)); } else if(!logy) min.y = 0;

    int tickCount[2]={};
    for(uint axis: range(2)) { //Ceils maximum using a number in the preferred sequence
        real subExponent = log10(max[axis]) - floor(log10(max[axis]));
        for(auto a: (real[][2]){{1,5}, {1.2,6}, {1.25,5}, /*{1.5,6},*/ {1.6,8}, {2,10}, {2.5,5}, {3,6}, {4,8}, {5,5}, {6,6}, {8,8}, {10,5}})
            if(log10(a[0]) >= subExponent) { max[axis] = a[0]*exp10(floor(log10(max[axis]))); tickCount[axis] = a[1]; break; }
    }

    // Configures ticks
    array<Text> ticks[2]; int2 tickLabelSize = 0;
    for(uint axis: range(2)) {
        int precision = ::max(0., ceil(-log10(max[axis]/tickCount[axis])));
        for(uint i: range(tickCount[axis]+1)) {
            real value = /*min[axis]+*/(max[axis]/*-min[axis]*/)*i/tickCount[axis];
            String label = ftoa(value, precision, 0, value>=10e5 ? 3 : 0);
            assert(label);
            ticks[axis] << Text(label);
            tickLabelSize = ::max(tickLabelSize, ticks[axis][i].sizeHint());
        }
    }
    int left=tickLabelSize.x, top=tickLabelSize.y, bottom=tickLabelSize.y;
    int right=::max(tickLabelSize.x, tickLabelSize.x/2+Text(format(Bold)+xlabel).sizeHint().x);

    // Colors
    buffer<vec4> colors(dataSets.size);
    if(colors.size==1) colors[0] = black;
    else if(colors.size==2) colors[0] = red, colors[1] = blue;
    else for(uint i: range(colors.size)) colors[i]=vec4(HSVtoRGB(2*PI*i/colors.size,1,1),1.f); //FIXME: constant intensity

    int2 pen=position;
    {Text text(format(Bold)+title); text.render(pen+int2((size.x-text.sizeHint().x)/2,top)); pen.y+=text.sizeHint().y; } // Title
    for(uint i: range(legends.size)) {Text text(legends[i], 16, colors[i]); text.render(pen+int2(size.x-right-text.sizeHint().x,top)); pen.y+=text.sizeHint().y; } // Legend

    // Transforms data positions to render positions
    auto point = [&](vec2 p)->vec2{
        p = (p-min)/(max-min);
        if(logx) p.x = ln(1+(e-1)*p.x);
        if(logy) p.y = ln(1+(e-1)*p.y);
        return vec2(position.x+left+p.x*(size.x-left-right),position.y+2*top+(1-p.y)*(size.y-2*top-bottom));
    };

    // Draws axis and ticks
    {vec2 O=vec2(min.x, 0), end = vec2(max.x, 0); // X
        line(point(O), point(end));
        for(uint i: range(tickCount[0]+1)) {
            int2 p (point(O+(i/float(tickCount[0]))*(end-O)));
            line(p, p+int2(0,-4));
            Text& tick = ticks[0][i];
            tick.render(p + int2(-tick.textSize.x/2, 0) );
        }
        {Text text(format(Bold)+xlabel); text.render(int2(point(end))+int2(tickLabelSize.x/2, -text.sizeHint().y/2));}
    }
    {vec2 O=vec2(0, min.y), end = vec2(0, max.y); // Y (FIXME: factor)
        line(point(O), point(end));
        for(uint i: range(tickCount[1]+1)) {
            int2 p (point(O+(i/float(tickCount[1]))*(end-O)));
            line(p, p+int2(4,0));
            Text& tick = ticks[1][i];
            tick.render(p + int2(-tick.textSize.x, -tick.textSize.y/2) );
        }
        {Text text(format(Bold)+ylabel); text.render(int2(point(end))+int2(-text.sizeHint().x/2, -text.sizeHint().y-tickLabelSize.y/2));}
    }

    // Plots data points
    for(uint i: range(dataSets.size)) {
        vec4 color = colors[i];
        const auto& data = dataSets[i];
        vec2 points[data.size()];
        for(uint i: range(data.size())) points[i] = point( vec2(data.keys[i],data.values[i]) );
        for(uint i: range(data.size()-1)) line(points[i], points[i+1], color);
    }
    ::resolution = resolution;
}

