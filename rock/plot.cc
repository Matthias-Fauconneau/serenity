#include "view.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "sample.h"

struct Plot : Widget {
    NonUniformSample data; String title, xlabel, ylabel;
    Plot(NonUniformSample&& data, const string& title, const string& xlabel="X"_, const string& ylabel="Y"_) :
        data(move(data)),title(String(title)),xlabel(String(xlabel)),ylabel(String(ylabel)){}
    void render(int2 position, int2 size) override {
        Text(title).render(position);
        if(!data) return;
        vec2 min=0, max=0;
        for(auto point: data) {
            vec2 p(point.key,point.value);
            min=::min(min,p);
            max=::max(max,p);
        }
        min = 0;
        int2 points[data.size()];
        for(uint i: range(data.size())) { vec2 p = (vec2(data.keys[i],data.values[i])-min)/(max-min); p.y=1-p.y; points[i]= position+int2(p*vec2(size)); }
        for(uint i: range(data.size()-1)) line(points[i],points[i+1]);
        //line(position+int2(0,size.y/2),position+int2(size.x,size.y/2), darkGray); //TODO: axes
    }
};

class(PlotView, View), Widget {
    array<Plot> plots;

    bool view(shared<Result>&& result) {
        if(!endsWith(result->metadata,"tsv"_)) return false;
        plots << Plot(parseNonUniformSample(result->data), result->name); //TODO: axis label from annotations
        window.localShortcut(Escape).connect(&window, &Window::destroy);
        window.backgroundColor = 1;
        window.setTitle(result->name);
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
