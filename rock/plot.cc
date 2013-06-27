#include "view.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "sample.h"
#include "math.h"

/// Returns the ceiling preferred number in the {1, 2.5, 5, 10} sequence
real preferredNumber(real value) {
    real subExponent = log10(value) - floor(log10(value));
    for(real a: (real[]){1, 2.5, 5, 10}) if(log10(a) >= subExponent) return a*exp10(floor(log10(value)));
    error("Unreachable"_);
}

struct Plot : Widget {
    NonUniformSample data; String title, xlabel, ylabel;
    Plot(NonUniformSample&& data, const string& title, const string& xlabel=""_, const string& ylabel=""_) :
        data(move(data)),title(String(title)),xlabel(String(xlabel)),ylabel(String(ylabel)){ assert_(this->data); }
    void render(int2 position, int2 size) override {
        {Text text(title); text.render(position+int2((size.x-text.sizeHint().x)/2,0));}

        // Computes axis scales
        vec2 min=0, max=0;
        for(auto point: data) {
            vec2 p(point.key,point.value);
            min=::min(min,p);
            max=::max(max,p);
        }
        min = 0, max.x = preferredNumber(max.x), max.y = preferredNumber(max.y);

        // Configures ticks
        Text ticks[2][6]; int2 tickLabelSize = 0;
        for(uint axis: range(2)) {
            int precision = ::max(0., ceil(-log10(max[axis]/5)));
            for(uint i: range(6)) {
                String label = ftoa(max[axis]*i/5, precision);
                assert_(label);
                ticks[axis][i] = Text(label);
                tickLabelSize = ::max(tickLabelSize, ticks[axis][i].sizeHint());
            }
        }
        int margin = ::max(tickLabelSize.x, tickLabelSize.y), left=margin, right=margin, top=margin, bottom=margin;
        // Transforms data positions to render positions
        auto point = [&](vec2 p)->int2{ p = (p-min)/(max-min); return position+int2(left+p.x*(size.x-left-right),top+(1-p.y)*(size.y-top-bottom)); };

        // Draws axis and ticks
        {vec2 end = vec2(max.x, min.y); // X
            line(point(min), point(end));
            for(uint i: range(6)) {
                int2 p = point(min+(i/5.f)*(end-min));
                line(p, p+int2(0,-4));
                Text& tick = ticks[0][i];
                tick.render(p + int2(-tick.textSize.x/2, 0) );
            }
            {Text text(format(Bold)+xlabel); text.render(point(end)+int2(tickLabelSize.x/2, -text.sizeHint().y/2));}
        }
        {vec2 end = vec2(min.x, max.y); // Y
            line(point(min), point(end));
            for(uint i: range(6)) {
                int2 p = point(min+(i/5.f)*(end-min));
                line(p, p+int2(4,0));
                Text& tick = ticks[1][i];
                tick.render(p + int2(-tick.textSize.x, -tick.textSize.y/2) );
            }
            {Text text(format(Bold)+ylabel); text.render(point(end)+int2(-text.sizeHint().x/2, -text.sizeHint().y-tickLabelSize.y));}
        }

        // Plots data points
        int2 points[data.size()];
        for(uint i: range(data.size())) points[i] = point( vec2(data.keys[i],data.values[i]) );
        for(uint i: range(data.size()-1)) line(points[i],points[i+1]);
    }
};

class(PlotView, View), Widget {
    array<Plot> plots;

    PlotView() {
        window.localShortcut(Escape).connect([]{exit();});
        window.backgroundColor = 1;
    }

    bool view(shared<Result>&& result) {
        if(!endsWith(result->metadata,"tsv"_)) return false;
        string x,y; { TextData s(result->metadata); y = s.until('('); x = s.until(')'); }
        string title = result->name; { TextData s(result->data); if(s.match('#')) title=s.until('\n'); }
        plots << Plot(parseNonUniformSample(result->data), title, x, y); //TODO: axis label from annotations
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
