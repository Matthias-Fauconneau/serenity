#include "view.h"
#include "text.h"
#include "interface.h"
#include "window.h"
#include "sample.h"
#include "plot.h"

class(TextView, View), virtual Text {
    bool view(const string& metadata, const string& name, const buffer<byte>& data) override {
        if(text) return false;
        if(endsWith(metadata, "scalar"_)) { setText(name+": "_+data); title = String(name); return true; }
        else if(endsWith(metadata,"text"_)||endsWith(metadata,"label"_)||endsWith(metadata,("size"_))||endsWith(metadata,("map"_))) {
            assert_(data && isUTF8(data), name, metadata); setText(data); title = String(name); return true;
        }
        return false;
    }
    string name() override { return title; }
    String title;
};

class(ImageView, View), virtual ImageWidget {
    Image image;
    ImageView():ImageWidget(image){}
    bool view(const string&, const string& name, const buffer<byte>& data) override {
        if(image || !imageFileFormat(data)) return false;
        image = decodeImage(data);
        title = String(name);
        return image ? true : false;
    }
    int2 sizeHint() override { while(!(image.size()<displaySize)) image=resize(image,image.size()/2); return ImageWidget::sizeHint(); }
    string name() override { return title; }
    String title;
};

class(PlotView, View), virtual Plot {
    bool view(const string& metadata, const string& name, const buffer<byte>& data) override {
        if(!endsWith(metadata,"tsv"_)) return false;
        string xlabel,ylabel; { TextData s(metadata); ylabel = s.until('('); xlabel = s.until(')'); }
        string legend=name; string title=legend; bool logx=false,logy=false;
        {TextData s(data); if(s.match('#')) title=s.until('\n'); if(s.match("#logx\n"_)) logx=true; if(s.match("#logy\n"_)) logy=true; }
        map<real,real> dataSet = parseNonUniformSample(data);
        if(!this->title) this->title=String(title), this->xlabel=String(xlabel), this->ylabel=String(ylabel), this->log[0]=logx, this->log[1]=logy;
        if((this->title && this->title!=title) && !(this->xlabel == xlabel && this->ylabel == ylabel && this->log[0]==logx && this->log[1]==logy)) return false;
        assert_(this->xlabel == xlabel && this->ylabel == ylabel && this->log[0]==logx && this->log[1]==logy);
        dataSets << move(dataSet);
        legends << String(legend);
        return true;
    }
    int2 sizeHint() override { return int2(1080,1080*3/4); }
    string name() override { return title; }
};
