#include "plot.h"
#include "window.h"
#include "display.h"
#include "text.h"

struct Ticks { float max; uint tickCount; };
uint subExponent(float& value) {
    real subExponent = exp10(log10(abs(value)) - floor(log10(abs(value))));
    for(auto a: (real[][2]){{1,5}, {1.2,6}, {1.25,5}, {1.6,8}, {2,10}, {2.5,5}, {3,3}, {4,8}, {5,5}, {6,6}, {8,8}, {9.6,8}, {10,5}})
        if(a[0] >= subExponent) { value=sign(value)*a[0]*exp10(floor(log10(abs(value)))); return a[1]; }
    error("No matching subexponent for"_, value);
}

int2 Plot::sizeHint() { return int2(-1080*4/3/2, -1080/2); }
void Plot::render(int2 position, int2 size) {
    int resolution = ::resolution; ::resolution = 1.5*96; // Scales the size of all text labels
    // Computes axis scales
    vec2 min=vec2(+__builtin_inf()), max=vec2(-__builtin_inf());
    for(const auto& data: dataSets) for(auto point: data) {
        vec2 p(point.key,point.value);
        assert(isNumber(p.x) && isNumber(p.y), p);
        min=::min(min,p);
        max=::max(max,p);
    }
    if(!logx && min.x>0) min.x = 0;
    if(!logy && min.y>0) min.y = 0;
    min.y = -39, max.y = 10;//FIXME: custom range

    int tickCount[2]={};
    for(uint axis: range(2)) { //Ceils maximum using a number in the preferred sequence
        if(max[axis]>-min[axis]) {
            tickCount[axis] = subExponent(max[axis]);
            if(min[axis] < 0) {
                float tickWidth = max[axis]/tickCount[axis];
                min[axis] = floor(min[axis]/tickWidth)*tickWidth;
                tickCount[axis] += -min[axis]/tickWidth;
            }
        } else {
            tickCount[axis] = subExponent(min[axis]);
            if(max[axis] > 0) {
                float tickWidth = -min[axis]/tickCount[axis];
                max[axis] = ceil(max[axis]/tickWidth)*tickWidth;
                tickCount[axis] += max[axis]/tickWidth;
            }
        }
    }

    // Configures ticks
    array<Text> ticks[2]; int2 tickLabelSize = 0;
    for(uint axis: range(2)) {
        int precision = ::max(0., ceil(-log10(max[axis]/tickCount[axis])));
        for(uint i: range(tickCount[axis]+1)) {
            real value = min[axis]+(max[axis]-min[axis])*i/tickCount[axis];
            if(axis==0 && value==0) { ticks[axis] << Text(); continue; } // Skips X origin tick (overlaps)
            String label = ftoa(value, precision, 0, value>=10e5 ? 3 : 0);
            assert(label);
            ticks[axis] << Text(label);
            tickLabelSize = ::max(tickLabelSize, ticks[axis][i].sizeHint());
        }
    }

    // Margins
    int left=tickLabelSize.x*3./2, top=tickLabelSize.y, bottom=tickLabelSize.y;
    int right=::max(tickLabelSize.x, tickLabelSize.x/2+Text(format(Bold)+xlabel).sizeHint().x);

    // Colors
    buffer<vec4> colors(dataSets.size);
    if(colors.size==1) colors[0] = black;
    else if(colors.size==2) colors[0] = red, colors[1] = blue;
    else for(uint i: range(colors.size)) colors[i]=vec4(HSVtoRGB(2*PI*i/colors.size,1,1),1.f); //FIXME: constant intensity

    int2 pen=position;
    {Text text(format(Bold)+title); text.render(pen+int2((size.x-text.sizeHint().x)/2,top)); pen.y+=text.sizeHint().y; } // Title
    assert(legends.size==0 || legends.size == dataSets.size);
    if(legendPosition&1) pen.x += size.x-right;
    if(legendPosition&2) {
        pen.y += size.y-bottom-tickLabelSize.y/2;
        for(uint i: range(legends.size)) pen.y -= Text(legends[i], 16, colors[i]).sizeHint().y;
    } else {
        pen.y += top;
    }
    for(uint i: range(legends.size)) { // Legend
        Text text(legends[i], 16, colors[i]); text.render(pen+int2(-text.sizeHint().x,0)); pen.y+=text.sizeHint().y;
    }

    // Transforms data positions to render positions
    auto point = [&](vec2 p)->vec2{
        // Converts min/max to log (for point(vec2)->vec2)
        vec2 lmin = vec2(logx ? log2(min.x) : min.x, logy ? log2(min.y) : min.y);
        vec2 lmax = vec2(logx ? log2(max.x) : max.x, logy ? log2(max.y) : max.y);
        if(logx) { assert(p.x>0, p.x); p.x = log2(p.x); }
        if(logy) { assert(p.y>0, p.y); p.y = log2(p.y); }
        p = (p-lmin)/(lmax-lmin);
        return vec2(position.x+left+p.x*(size.x-left-right),position.y+2*top+(1-p.y)*(size.y-2*top-bottom));
    };

    // Draws axis and ticks
    {vec2 O=min, end = vec2(max.x, min.y); // X
        line(point(O), point(end));
        for(uint i: range(logx, tickCount[0]+1)) {
            int2 p (point(O+(i/float(tickCount[0]))*(end-O)));
            line(p, p+int2(0,-4));
            Text& tick = ticks[0][i];
            tick.render(p + int2(-tick.textSize.x/2, 0) );
        }
        {Text text(format(Bold)+xlabel); text.render(int2(point(end))+int2(tickLabelSize.x/2, -text.sizeHint().y/2));}
    }
    {vec2 O=min, end = vec2(min.x, max.y); // Y (FIXME: factor)
        line(point(O), point(end));
        for(uint i: range(logy, tickCount[1]+1)) {
            int2 p (point(O+(i/float(tickCount[1]))*(end-O)));
            line(p, p+int2(4,0));
            Text& tick = ticks[1][i];
            tick.render(p + int2(-tick.textSize.x-left/6, -tick.textSize.y/2) );
        }
        {Text text(format(Bold)+ylabel);
            text.render(int2(point(end))+int2(-text.sizeHint().x/2, -text.sizeHint().y-tickLabelSize.y/2));}
    }

    // Plots data points
    for(uint i: range(dataSets.size)) {
        vec4 color = colors[i];
        const auto& data = dataSets[i];
        vec2 points[data.size()];
        for(uint i: range(data.size())) points[i] = point( vec2(data.keys[i],data.values[i]) );
        if(plotPoints) for(uint i: range(data.size())) {
            vec2 p = round(points[i]);
            const int pointRadius = 4;
            line(p-vec2(pointRadius, 0), p+vec2(pointRadius, 0), color);
            line(p-vec2(0, pointRadius), p+vec2(0, pointRadius), color);
        }
        if(plotLines) for(uint i: range(data.size()-1)) line(points[i], points[i+1], color);
    }
    ::resolution = resolution;
}

