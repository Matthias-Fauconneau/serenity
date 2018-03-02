#include "plot.h"
#include "text.h"
#include "color.h"

static inline double log2(double x) { return __builtin_log2(x); }
static inline double exp2(double x) { return __builtin_exp2(x); }
static inline double log10(double x) { return __builtin_log10(x); }
static inline double exp10(double x) { return exp2(log2(10)*x); }

struct Ticks { float max; uint tickCount; };
static uint subExponent(float& value) {
    double subExponent = exp10(log10(abs(value)) - floor(log10(abs(value))));
    for(auto a: (double[][2]){{1,5},{1.2,6},{1.4,7},{1.6,4},{2,2},{2.5,5},{3,3},{3.2,8},{4,4},{5,5},{6,6},{8,8},{10,5}}) {
        if(a[0] >= subExponent-0x1p-52) {
            value = (value>0?1:-1)*a[0]*exp10(floor(log10(abs(value))));
            return a[1];
        }
    }
    error("No matching subexponent for"_, value);
}

vec2 Plot::sizeHint(vec2 size) {
    if(!dataSets) return 0;
    else size.y = ::min(size.y, 3*size.x/4);
    return -size; // Expanding
}
void Plot::render(RenderTarget2D& target, vec2 offset, vec2 size) {
    assert_(offset == vec2(0) && size == vec2(0/*target.size*/), offset, size, target.size);
    if(!size) size=target.size;
    vec2 min=vec2(+__builtin_inff()), max=vec2(-__builtin_inff());
    // Computes axis scales
    for(const map<float,float>& data: dataSets.values) {
        if(!data.values) continue; // FIXME
        for(auto point: data) {
            vec2 p(point.key, point.value);
            assert_(isNumber(p));
            min=::min(min,p);
            max=::max(max,p);
        }
    }
    for(size_t i: range(2)) if(!log[i]) { if(i>0 && min[i]>0) min[i] = 0; if(max[i]<0) max[i] = 0; }
    if(this->min.x < this->max.x) { min.x=this->min.x; max.x=this->max.x; }; // Custom scales
    if(this->min.y < this->max.y) min.y=this->min.y; // Custom scales

    if(!(min.x < max.x && min.y < max.y)) { ::log("Not enough data for plot"); return; }
    assert_(isNumber(min) && isNumber(max) && min.x < max.x && min.y < max.y);

    int tickCount[2]={};
    for(size_t axis: range(2)) { // Ceils maximum using a number in the preferred sequence
        if(max[axis]>-min[axis]) {
            if(log[axis]) { //FIXME
                max[axis] = exp2(ceil(log2(max[axis])));
                tickCount[axis] = ceil(log2(max[axis]/min[axis]));
                min[axis] = max[axis]*exp2( -tickCount[axis] );
            } else {
                tickCount[axis] = subExponent(max[axis]);
                if(min[axis] < 0) {
                    float tickWidth = max[axis]/tickCount[axis];
                    min[axis] = floor(min[axis]/tickWidth)*tickWidth;
                    tickCount[axis] += -min[axis]/tickWidth;
                } else min[axis] = 0;
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

    const float textSize = 16; const string fontName = "DejaVuSans"_;//"latinmodern-math"_;
    // Configures ticks
    struct Tick : Text { float value; Tick(float value, string label, float textSize, string fontName, int2 align) : Text(label, textSize, 0,1,0, fontName, true, 1, align), value(value) {} };
    array<Tick> ticks[2]; vec2 tickLabelSize = 0;
    for(size_t axis: range(2)) {
        uint precision = ::max(2./*1.*/, ceil(-log10(::max(-min[axis],max[axis])/tickCount[axis])));
        for(size_t i: range(tickCount[axis]+1)) {
            float lmin = log[axis] ? log2(min[axis]) : min[axis];
            float lmax = log[axis] ? log2(max[axis]) : max[axis];
            float value = lmin+(lmax-lmin)*i/tickCount[axis];
            if(log[axis]) value = exp2(value);
            String label = str(value, precision, 0u);
            if(axis==1) label = str(value*100, 1u)+"%"; // HACK
            assert(label);
            ticks[axis].append(value, label, textSize, fontName, axis==0?int2(-1,-1):int2(-1,0));
            tickLabelSize = ::max(tickLabelSize, vec2(ticks[axis][i].sizeHint().x, textSize));
        }
    }

    // Evaluates margins
    float left = ::max(Text(ylabel, textSize, 0,1,0, fontName).sizeHint().x/2, tickLabelSize.x+textSize);
    float top = tickLabelSize.y+textSize;
    float bottom = tickLabelSize.y*3/2;
    float right = tickLabelSize.x/2 + Text(xlabel, textSize, 0,1,0, fontName).sizeHint().x;

    const float tickLength = 4;

    // Evaluates colors
    buffer<bgr3f> colors(dataSets.size());
    if(colors.size<=4) for(size_t i: range(colors.size)) colors[i] = ref<bgr3f>{black,red,green,blue}[i];
    else for(size_t i: range(colors.size))
        colors[i] = clamp(bgr3f(0), LChuvtoBGR(53,179, 2*π*i/colors.size), bgr3f(1));

    // Orders legend by last point value
    buffer<uint> order (dataSets.size(), 0);
    for(size_t i: range(dataSets.size())) order.append(i);
    for(int i: range(1, order.size)) {
        while(i > 0) {
            if(!dataSets.values || !dataSets.values[order[i-1]].values || !dataSets.values[order[i]].values) break;
            if(!(dataSets.values[order[i-1]].values.last() < dataSets.values[order[i]].values.last())) break;
            swap(order[i-1], order[i]);
            i--;
        }
    }

    // Transforms data positions to render positions
    auto point = [&](vec2 p)->vec2{
        // Converts min/max to log (for point(vec2)->vec2)
        for(int axis: range(2)) {
            float lmin = log[axis] ? log2(min[axis]) : min[axis];
            float lmax = log[axis] ? log2(max[axis]) : max[axis];
            if(log[axis]) p[axis] = log2(p[axis]);
            p[axis] = (p[axis]-lmin)/(lmax-lmin);
        }
        return vec2(left+p.x*(size.x-left-right),/*2**/top+(1-p.y)*(size.y-/*2**/top-bottom));
    };

    {
        int2 pen = 0;
        /*if(name) { // Title
            Text text(bold(name), textSize, 0,1,0, fontName);
            text.render(target.offset(vec2(pen+int2((size.x-text.sizeHint().x)/2,top))));
            pen.y += text.sizeHint().y;
        }*/
        if(legendPosition&1) pen.x += size.x - right;
        else pen.x += left + 2*tickLength;
        if(legendPosition&2) {
            pen.y += size.y - bottom - tickLabelSize.y/2;
            pen.y -= dataSets.size()*textSize;
        } else {
            pen.y += top;
        }

        for(uint i: order) { // Legend
            if(!dataSets.values[i].values) continue;
            Text text(dataSets.keys[i], textSize, colors[order.indexOf(i)/*Do not change color order*/], 1,0, fontName);
            text.render(target, vec2(pen+int2(legendPosition&1 ? -text.sizeHint().x : 0,0)), 0);
            pen.y += textSize;
            //text.render(target, vec2(float(pen.x), point(vec2(0,dataSets.values[i].values.last())).y), 0);
        }
    }

    // Draws axis and ticks
    {vec2 O=vec2(min.x, min.y>0 ? min.y : max.y<0 ? max.y : 0), end = vec2(max.x, O.y); // X
        target.line(point(O), point(end));
        for(size_t i: range(tickCount[0]+1)) {
            Tick& tick = ticks[0][i];
            vec2 p(point(vec2(tick.value, O.y)));
            target.line(p, p+vec2(0,tickLength));
            tick.render(target, p + vec2(0, textSize), 0);
        }
        { Text text(xlabel, textSize, 0,1,0, fontName, true, 1, int2(0, 0));
            text.render(target, point(end)+vec2(text.sizeHint().x/2+textSize/2,0), 0);
        }
    }
    {vec2 O=vec2(min.x>0 ? min.x : max.x<0 ? max.x : 0, min.y), end = vec2(O.x, max.y); // Y
        target.line(point(O), point(end));
        for(size_t i: range(tickCount[1]+1)) {
            vec2 p (point(O+(i/float(tickCount[1]))*(end-O)));
            target.line(p, p+vec2(-tickLength,0));
            Text& tick = ticks[1][i];
            tick.render(target, p + vec2(-tick.sizeHint().x, 0), 0);
        }
        {Text text(ylabel, textSize, 0,1,0, fontName, true, 1, int2(0, 1));
            vec2 p = point(end) + vec2(0, -text.sizeHint().y);
            text.render(target, p, 0);
        }
    }

    // Plots data points
    for(size_t i: range(dataSets.size())) {
        bgr3f color = colors[order.indexOf(i)];
        assert_(bgr3f(0) <= color && color <= bgr3f(1), color);
        const auto& data = dataSets.values[i];
        buffer<vec2> points = apply(data.size(), [&](size_t i){ return point( vec2(data.keys[i],data.values[i]) ); });
        if(plotPoints || points.size==1 || plotBandsY) for(vec2 p: points) {
            if(!isNumber(p)) continue;
            const int pointRadius = 2;
            if(i==0) {
                target.line(p-vec2(pointRadius, 0), p+vec2(pointRadius, 0), color);
                target.line(p-vec2(0, pointRadius), p+vec2(0, pointRadius), color);
            } else if(i==1) {
                float d = pointRadius/sqrt(2.);
                target.line(p+vec2(-d, -d), p+vec2(d, d), color);
                target.line(p+vec2(-d, d), p+vec2(d, -d), color);
            } else if(i==2) {
                float d = pointRadius/sqrt(2.);
                target.line(p+vec2(-d, -d), p+vec2(d, -d), color);
                target.line(p+vec2(d, -d), p+vec2(d, d), color);
                target.line(p+vec2(d, d), p+vec2(-d, d), color);
                target.line(p+vec2(-d, d), p+vec2(-d, -d), color);
            } else if(i==3) {
                float d = pointRadius;
                target.line(p-vec2(0, -d), p+vec2(d, 0), color);
                target.line(p-vec2(d, 0), p+vec2(0, d), color);
                target.line(p-vec2(0, d), p+vec2(-d, 0), color);
                target.line(p-vec2(-d, 0), p+vec2(0, -d), color);
            }
        }
        if(plotLines && !plotBandsY) for(size_t i: range(points.size-1)) {
            if(!isNumber(points[i]) || !isNumber(points[i+1])) continue;
            target.line(points[i], points[i+1], color);
        }
        if(plotBandsY && points) { // Y bands
            Span span[2] = {{points[0].x,points[0].y,points[0].y},{points[0].x,points[0].y,points[0].y}};
            for(vec2 p: points.slice(1)) {
                if(p.x == span[1].x) {
                    span[1].min = ::min(span[1].min, p.y);
                    span[1].max = ::max(span[1].max, p.y);
                } else { // Commit band
                    target.line(vec2(span[0].x, span[0].min), vec2(span[1].x, span[1].min), color);
                    target.line(vec2(span[0].x, span[0].max), vec2(span[1].x, span[1].max), color);
                    target.trapezoidY(span[0], span[1], color, 1.f/2);
                    span[0] = span[1];
                    span[1] = {p.x, p.y, p.y};
                }
            }
            target.line(vec2(span[0].x, span[0].min), vec2(span[1].x, span[1].min), color);
            target.line(vec2(span[0].x, span[0].max), vec2(span[1].x, span[1].max), color);
            target.trapezoidY(span[0], span[1], color, 1.f/2);
        }
    }
}
