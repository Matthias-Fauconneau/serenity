#include "process.h"
#include "volume-operation.h"
#include "sample.h"
#include "widget.h"
#include "display.h"
#include "window.h"
#include "text.h"
#include "interface.h"
#include "plot.h"
#include "png.h"

/// Presents all important informations extracted from a rock data set
class(Summary, Tool) {
    string parameters() const override { return "path denoise"_; }
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, Process& process) override {
        const int2 pageSize = int2(round(1024/sqrt(2.)), 1024);
        array<shared<Result>> hold; // Prevents result files from being recycled
        Dict args = copy(arguments); args.insert("source.cylinder"_);
        VBox vbox (Linear::Share, Linear::Expand);
        HList<Text> header;
        for(string result : (string[]){"name"_, "resolution"_, "voxelSize"_, "physicalSize"_}) header << Text(format(Bold)+process.getResult(result, args)->data);
        vbox << &header;
        UniformGrid<Item> slices;
        int scale=0;
        for(string target : (string[]){"source"_, "denoised"_, "thresholded"_, "distance"_, "skeleton"_, "maximum"_}) {
            if(target=="denoised"_  && args.value("denoise"_)=="0"_) continue;
            shared<Result> result = process.getResult(target, args);
            Image slice = ::slice(toVolume(result),0.5f, true, false, true);
            if(target=="distance"_ || target=="maximum"_) for(byte4& pixel: slice.buffer) { pixel=byte4(0xFF)-pixel; pixel.a=0xFF; } // Inverts color for print
            if(target=="skeleton"_) for(byte4& pixel: slice.buffer) { pixel = pixel.g ? 0 : 0xFF; pixel.a=0xFF; } // Maximize contrast for print
            for(byte4& pixel: slice.buffer) { // sRGB conversion _after_ inversion
                extern uint8 sRGB_lookup[256]; //FIXME: unnecessary quantization loss on rounding linear values to 8bit
                pixel = byte4(sRGB_lookup[pixel.b], sRGB_lookup[pixel.g], sRGB_lookup[pixel.r], pixel.a);
            }
            if(!scale) scale = max(1.,round((real)pageSize.x/slice.size().x));
            assert_(scale>0 && scale<=3,scale);
            slices << Item(scale==1 ? move(slice) : resize(slice,slice.size()/scale), target, 16, true);
            assert_(slices.last().icon.image.size());
            hold << move(result);
        }
        vbox << &slices;
        output(outputs, "slices"_, "png"_, [&]{VBox vbox (Linear::Share, Linear::Expand); vbox<<&header<<&slices; return encodePNG(renderToImage(&vbox, int2(2,1)*pageSize, 1.5*96/*dpi*/));});
        UniformGrid<Plot> plots;
        assert_(args.at("threshold"_)=="otsu"_);
        {Plot otsu; otsu.title = String("Interclass deviation versus threshold"_), otsu.xlabel=String("μ"_), otsu.ylabel=String("σ"_);
            otsu.legends << String("Radiodensity probability"_); otsu.dataSets << parseNonUniformSample(process.getResult("distribution-radiodensity-normalized"_, args)->data);
            otsu.legends << String("Interclass deviation (Otsu)"_); otsu.dataSets << parseNonUniformSample(process.getResult("otsu-interclass-deviation-normalized"_, args)->data);
            plots << move(otsu);}
        {Plot plot; plot.title = String("Pore size distribution"_), plot.xlabel=String("r [μm]"_), plot.ylabel=String("V"_);
            plot.legends << String("Probability density estimation"); plot.dataSets << parseNonUniformSample(process.getResult("distribution-radius"_, args)->data);
            plots << move(plot);}
        {Plot plot; plot.title = String("Representative elementary volume"_), plot.xlabel=String("R [μm]"_), plot.ylabel=String("ε"_);
            plot.legends << String("Pore size distribution deviation"); plot.dataSets << parseNonUniformSample(process.getResult("ε(R)"_, args)->data);
            plots << move(plot);}
        {Plot plot; plot.title = String("Largest bottleneck radius"_), plot.xlabel=String("r [μm]"_), plot.ylabel=String("V"_);
            plot.legends << String("Unconnected volume"); plot.dataSets << parseNonUniformSample(process.getResult("unconnected(λ)"_, args)->data);
            plot.legends << String("Connected volume"); plot.dataSets << parseNonUniformSample(process.getResult("connected(λ)"_, args)->data);
            plots << move(plot);}
        vbox << &plots;
        HList<Text> properties;
        for(auto result : map<string, string>({"porosity"_,"critical-radius"_,"mean-radius"_,"representative-radius"_},{"Porosity"_,"Critical bottleneck radius (μm)"_,"Mean pore radius (μm)"_,"Representative radius (μm)"_}))
            properties << Text(result.value+": "_+format(Bold)+process.getResult(result.key, args)->data);
        vbox << &properties;
        output(outputs, "plots"_, "png"_, [&]{VBox vbox (Linear::Share, Linear::Expand); vbox<<&plots<<&properties; return encodePNG(renderToImage(&vbox, int2(2,1)*pageSize, 1.5*96/*dpi*/));});
        output(outputs, "summary"_, "png"_, [&]{return encodePNG(renderToImage(&vbox, 2*pageSize, 1.5*96/*dpi*/));});
    }
};
