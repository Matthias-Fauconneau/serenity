#include "plot.h"
#include "view.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "math.h"

void Plot::render(int2 position, int2 size) {
    // Computes axis scales
    vec2 min=0, max=0;
    for(const auto& data: dataSets) for(auto point: data) {
        vec2 p(point.key,point.value);
        min=::min(min,p);
        max=::max(max,p);
    }
    if(!logx && min.x>0) min.x = 0;
    if(min.y<0) { assert(!logy); min.y=-(max.y=::max(-min.y,max.y)); } else if(!logy) min.y = 0;

    int tickCount[2]={};
    for(uint axis: range(2)) { //Ceils maximum using a number in the preferred sequence
        real subExponent = log10(max[axis]) - floor(log10(max[axis]));
        for(auto a: (real[][2]){{1,5}, {1.2,6}, {1.25,5}, {1.5,6}, {1.6,8}, {2,10}, {2.5,5}, {5,5}, {6,6}, {8,8}, {10,5}})
            if(log10(a[0]) >= subExponent) { max[axis] = a[0]*exp10(floor(log10(max[axis]))); tickCount[axis] = a[1]; break; }
    }

    // Configures ticks
    array<Text> ticks[2]; int2 tickLabelSize = 0;
    for(uint axis: range(2)) {
        int precision = ::max(0., ceil(-log10(max[axis]/tickCount[axis])));
        for(uint i: range(tickCount[axis]+1)) {
            String label = ftoa(min[axis]+(max[axis]-min[axis])*i/tickCount[axis], precision);
            assert_(label);
            ticks[axis] << Text(label);
            tickLabelSize = ::max(tickLabelSize, ticks[axis][i].sizeHint());
        }
    }
    int margin = ::max(tickLabelSize.x, tickLabelSize.y), left=margin, top=margin, bottom=margin;
    int right=::max(tickLabelSize.x, tickLabelSize.x/2+Text(format(Bold)+xlabel).sizeHint().x);

    {Text text(format(Bold)+title); text.render(position+int2((size.x-text.sizeHint().x)/2,top));} // Title
    // Legend
    buffer<vec4> colors(legends.size); for(uint i: range(colors.size)) colors[i]=vec4(HSVtoRGB(2*PI*i/colors.size,1,1),1.f); //FIXME: constant intensity
    if(legends.size>1) {
        int2 pen=position; for(uint i: range(legends.size)) {Text text(legends[i], 16, colors[i]); text.render(pen+int2(size.x-right-text.sizeHint().x,top)); pen.y+=text.sizeHint().y; }
    }

    // Transforms data positions to render positions
    auto point = [&](vec2 p)->vec2{
        p = (p-min)/(max-min);
        if(logx) p.x = ln(1+(e-1)*p.x);
        if(logy) p.y = ln(1+(e-1)*p.y);
        return vec2(position.x+left+p.x*(size.x-left-right),position.y+top+(1-p.y)*(size.y-top-bottom));
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
        {Text text(format(Bold)+ylabel); text.render(int2(point(end))+int2(-text.sizeHint().x/2, -text.sizeHint().y-tickLabelSize.y));}
    }

    // Plots data points
    assert_(dataSets.size == legends.size && dataSets.size == colors.size);
    for(uint i: range(dataSets.size)) {
        vec4 color = colors[i];
        const auto& data = dataSets[i];
        vec2 points[data.size()];
        for(uint i: range(data.size())) points[i] = point( vec2(data.keys[i],data.values[i]) );
        for(uint i: range(data.size()-1)) line(points[i], points[i+1], color);
    }
}

class(PlotView, View), virtual Plot {
    bool view(const string& metadata, const string& name, const buffer<byte>& data) {
        if(!endsWith(metadata,"tsv"_)) return false;
        string xlabel,ylabel; { TextData s(metadata); ylabel = s.until('('); xlabel = s.until(')'); }
        string legend=name; string title=legend; bool logx=false,logy=false;
        {TextData s(data); if(s.match('#')) title=s.until('\n'); if(s.match("#logx\n"_)) logx=true; if(s.match("#logy\n"_)) logy=true; }
        auto dataSet = parseNonUniformSample(data);
        if(!this->title) this->title=String(title), this->xlabel=String(xlabel), this->ylabel=String(ylabel), this->logx=logx, this->logy=logy;
        if(this->title && this->title!=title) return false;
        assert_(this->xlabel == xlabel && this->ylabel == ylabel && this->logx==logx && this->logy==logy);
        dataSets << move(dataSet);
        legends << String(legend);
        return true;
    }
    string name() override { return title; }
};
