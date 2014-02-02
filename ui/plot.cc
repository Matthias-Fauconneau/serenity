#include "plot.h"
#include "window.h"
#include "display.h"
#include "text.h"

struct Ticks { float max; uint tickCount; };
uint subExponent(float& value) {
    real subExponent = exp10(log10(abs(value)) - floor(log10(abs(value))));
    for(auto a: (real[][2]){{1,5}, {1.2,6}, {1.25,5}, {1.6,8}, {2,10}, {2.5,5}, {3,3}, {4,8}, {5,5}, {6,6}, {8,8}, {9.6,8}, {10,5}}) {
        if(a[0] >= subExponent-0x1p-52) { value=sign(value)*a[0]*exp10(floor(log10(abs(value)))); return a[1]; }
    }
    error("No matching subexponent for"_, value);
}

int2 Plot::sizeHint() { return int2(512, 512); }
void Plot::render(int2 position, int2 size) {
    int resolution = ::resolution; ::resolution = 1.5*96; // Scales the size of all text labels
    vec2 min=vec2(+__builtin_inf()), max=vec2(-__builtin_inf());
    if(this->min.x < this->max.x && this->min.y < this->max.y) min=this->min, max=this->max; // Custom scales
    else {  // Computes axis scales
        assert(dataSets);
        for(const auto& data: dataSets) {
            assert(data);
            for(auto point: data) {
                vec2 p(point.key,point.value);
                assert(isNumber(p.x) && isNumber(p.y), p);
                min=::min(min,p);
                max=::max(max,p);
            }
        }
        for(uint i: range(2)) if(!log[i]) { if(min[i]>0) min[i] = 0; if(max[i]<0) max[i] = 0; }
    }
    assert(min.x < max.x && min.y < max.y, min, max);

    int tickCount[2]={};
    for(uint axis: range(2)) { //Ceils maximum using a number in the preferred sequence
        if(max[axis]>-min[axis]) {
            if(log[axis]) { //FIXME
                max[axis] = exp(ceil(ln(max[axis])));
                tickCount[axis] = ceil(ln(max[axis]/min[axis]));
                min[axis] = max[axis]*exp( -tickCount[axis] );
            } else {
                tickCount[axis] = subExponent(max[axis]);
                if(min[axis] < 0) {
                    float tickWidth = max[axis]/tickCount[axis];
                    min[axis] = floor(min[axis]/tickWidth)*tickWidth;
                    tickCount[axis] += -min[axis]/tickWidth;
                }
            }
        } else {
            assert(!log[axis]); //FIXME
            tickCount[axis] = subExponent(min[axis]);
            if(max[axis] > 0) {
                float tickWidth = -min[axis]/tickCount[axis];
                max[axis] = -floor(-max[axis]/tickWidth)*tickWidth;
                tickCount[axis] += max[axis]/tickWidth;
            }
        }
    }

    // Configures ticks
    struct Tick : Text { float value; Tick(float value, const string& label):Text(label), value(value) {} };
    array<Tick> ticks[2]; int2 tickLabelSize = 0;
    for(uint axis: range(2)) {
        int precision = ::max(0., ceil(-log10(::max(-min[axis],max[axis])/tickCount[axis])));
        for(uint i: range(tickCount[axis]+1)) {
            float lmin = log[axis] ? ln(min[axis]) : min[axis];
            float lmax = log[axis] ? ln(max[axis]) : max[axis];
            real value = lmin+(lmax-lmin)*i/tickCount[axis];
            if(log[axis]) value = exp(value);
            String label = ftoa(value, precision, 0, value>=10e5 ? 3 : value <=10e-2 ? 1 : 0);
            assert(label);
            ticks[axis] <<Tick(value, label);
            tickLabelSize = ::max(tickLabelSize, ticks[axis][i].sizeHint());
        }
    }

    // Margins
    int left=tickLabelSize.x*3./2, top=tickLabelSize.y, bottom=tickLabelSize.y;
    int right=::max(tickLabelSize.x, tickLabelSize.x/2+Text(format(Bold)+xlabel).sizeHint().x);

    // Colors
    buffer<vec4> colors(dataSets.size);
    if(colors.size==1) colors[0] = black;
    else for(uint i: range(colors.size)) colors[i]=vec4(LChuvtoBGR(53,179,2*PI*i/colors.size),1.f);

    int2 pen=position;
    {Text text(format(Bold)+title,16); text.render(pen+int2((size.x-text.sizeHint().x)/2,top)); pen.y+=text.sizeHint().y; } // Title
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
        for(int axis: range(2)) {
            float lmin = log[axis] ? ln(min[axis]) : min[axis];
            float lmax = log[axis] ? ln(max[axis]) : max[axis];
            if(log[axis]) p[axis] = ln(p[axis]);
            p[axis] = (p[axis]-lmin)/(lmax-lmin);
        }
        return vec2(position.x+left+p.x*(size.x-left-right),position.y+2*top+(1-p.y)*(size.y-2*top-bottom));
    };

    // Draws axis and ticks
    {vec2 O=vec2(min.x, min.y>0 ? min.y : max.y<0 ? max.y : 0), end = vec2(max.x, O.y); // X
        line(point(O), point(end));
        for(uint i: range(tickCount[0]+1)) {
            Tick& tick = ticks[0][i];
            int2 p(point(vec2(tick.value, O.y)));
            line(p, p+int2(0,-4));
            tick.render(p + int2(-tick.textSize.x/2, -min.y > max.y ? -tick.textSize.y : 0) );
        }
        {Text text(format(Bold)+xlabel,16); text.render(int2(point(end))+int2(tickLabelSize.x/2, -text.sizeHint().y/2));}
    }
    {vec2 O=vec2(min.x>0 ? min.x : max.x<0 ? max.x : 0, min.y), end = vec2(O.x, max.y); // Y
        line(point(O), point(end));
        for(uint i: range(tickCount[1]+1)) {
            int2 p (point(O+(i/float(tickCount[1]))*(end-O)));
            line(p, p+int2(4,0));
            Text& tick = ticks[1][i];
            tick.render(p + int2(-tick.textSize.x-left/6, -tick.textSize.y/2) );
        }
        {Text text(format(Bold)+ylabel,16);
            text.render(int2(point(end))+int2(-text.sizeHint().x/2, -text.sizeHint().y-tickLabelSize.y/2));}
    }

    // Plots data points
    for(uint i: range(dataSets.size)) {
        vec4 color = colors[i];
        const auto& data = dataSets[i];
        buffer<vec2> points = apply(data.size(), [&](uint i){ return point( vec2(data.keys[i],data.values[i]) ); });
        if(plotPoints) for(uint i: range(data.size())) {
            int2 p = int2(round(points[i]));
            const int pointRadius = 2;
            line(p-int2(pointRadius, 0), p+int2(pointRadius, 0), color);
            line(p-int2(0, pointRadius), p+int2(0, pointRadius), color);
        }
        if(plotLines) for(uint i: range(data.size()-1)) line(points[i], points[i+1], color);
    }
    ::resolution = resolution;
}

