#include "view.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "sample.h"
#include "math.h"

struct Plot : Widget {
    Plot(const string& title, const string& xlabel, const string& ylabel, const string& legend, NonUniformSample&& data)
        : title(String(title)), xlabel(String(xlabel)),ylabel(String(ylabel)) { legends<<String(legend); dataSets << move(data); }

    void render(int2 position, int2 size) override {
        // Computes axis scales
        vec2 min=0, max=0;
        for(const auto& data: dataSets) for(auto point: data) {
            vec2 p(point.key,point.value);
            min=::min(min,p);
            max=::max(max,p);
        }
        min = 0;

        int tickCount[2]={};
        for(uint axis: range(2)) { //Ceils maximum using a number in the preferred sequence
            real subExponent = log10(max[axis]) - floor(log10(max[axis]));
            for(auto a: (real[][2]){{1,5}, {1.2,6}, {2.5,5}, {5,5}, {6,6}, {10,5}})
                if(log10(a[0]) >= subExponent) { max[axis] = a[0]*exp10(floor(log10(max[axis]))); tickCount[axis] = a[1]; break; }
        }

        // Configures ticks
        array<Text> ticks[2]; int2 tickLabelSize = 0;
        for(uint axis: range(2)) {
            int precision = ::max(0., ceil(-log10(max[axis]/tickCount[axis])));
            for(uint i: range(tickCount[axis]+1)) {
                String label = ftoa(max[axis]*i/tickCount[axis], precision);
                assert_(label);
                ticks[axis] << Text(label);
                tickLabelSize = ::max(tickLabelSize, ticks[axis][i].sizeHint());
            }
        }
        int margin = ::max(tickLabelSize.x, tickLabelSize.y), left=margin, right=margin, top=margin, bottom=margin;

        {Text text(format(Bold)+title); text.render(position+int2((size.x-text.sizeHint().x)/2,top));} // Title
        // Legend
        buffer<vec4> colors(legends.size); for(uint i: range(colors.size)) colors[i]=vec4(HSVtoRGB(2*PI*i/colors.size,1,1),1.f);
        if(legends.size>1) {
            int2 pen=position; for(uint i: range(legends.size)) {Text text(legends[i], 16, colors[i]); text.render(pen+int2(size.x-right-text.sizeHint().x,top)); pen.y+=text.sizeHint().y; }
        }

        // Transforms data positions to render positions
        auto point = [&](vec2 p)->int2{ p = (p-min)/(max-min); return position+int2(left+p.x*(size.x-left-right),top+(1-p.y)*(size.y-top-bottom)); };

        // Draws axis and ticks
        {vec2 end = vec2(max.x, min.y); // X
            line(point(min), point(end));
            for(uint i: range(tickCount[0]+1)) {
                int2 p = point(min+(i/float(tickCount[0]))*(end-min));
                line(p, p+int2(0,-4));
                Text& tick = ticks[0][i];
                tick.render(p + int2(-tick.textSize.x/2, 0) );
            }
            {Text text(format(Bold)+xlabel); text.render(point(end)+int2(tickLabelSize.x/2, -text.sizeHint().y/2));}
        }
        {vec2 end = vec2(min.x, max.y); // Y (FIXME: factor)
            line(point(min), point(end));
            for(uint i: range(tickCount[1]+1)) {
                int2 p = point(min+(i/float(tickCount[1]))*(end-min));
                line(p, p+int2(4,0));
                Text& tick = ticks[1][i];
                tick.render(p + int2(-tick.textSize.x, -tick.textSize.y/2) );
            }
            {Text text(format(Bold)+ylabel); text.render(point(end)+int2(-text.sizeHint().x/2, -text.sizeHint().y-tickLabelSize.y));}
        }

        // Plots data points
        assert_(dataSets.size == legends.size && dataSets.size == colors.size);
        for(uint i: range(dataSets.size)) {
            vec4 color = colors[i];
            const auto& data = dataSets[i];
            int2 points[data.size()];
            for(uint i: range(data.size())) points[i] = point( vec2(data.keys[i],data.values[i]) );
            for(uint i: range(data.size()-1)) line(points[i], points[i+1], color);
        }
    }

    String title, xlabel, ylabel;
    array<String> legends;
    array<NonUniformSample> dataSets;
};

class(PlotView, View), Widget {
    array<Plot> plots;

    PlotView() {
        window.localShortcut(Escape).connect([]{exit();});
        window.backgroundColor = 1;
    }

    bool view(string metadata, string name, const buffer<byte>& data) {
        if(!endsWith(metadata,"tsv"_)) return false;
        string x,y; { TextData s(metadata); y = s.until('('); x = s.until(')'); }
        string legend = name; string title=legend; { TextData s(data); if(s.match('#')) title=s.until('\n'); }
        auto sample = parseNonUniformSample(data);
        for(Plot& plot: plots) { // Tries to merge with an existing plot
            if(plot.title==title) {
                assert_(plot.xlabel == x && plot.ylabel == y && plot.dataSets.first().keys == sample.keys);
                plot.dataSets << move(sample);
                plot.legends << String(name);
                goto break_;
            }
        } /*else*/ plots << Plot(title, x, y, name, move(sample)); // New plot
        break_:;
        array<String> plotTitles; for(const Plot& plot: plots) plotTitles << copy(plot.title); window.setTitle(join(plotTitles,", "_));
        window.show();
        return true;
    }

    void render(int2 position, int2 size) override {
        const uint w = 1, h = plots.size;
        for(uint i : range(plots.size)) {
            int2 plotSize = int2(size.x/w,size.y/h);
            int2 plotPosition = position+int2(i%w,i/w)*plotSize;
            plots[i].render(plotPosition, plotSize);
        }
    }

    Window window {this,int2(1920/2,1080/2),"PlotView"_};
};
