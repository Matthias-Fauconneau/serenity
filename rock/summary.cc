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
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, Process& process) override {
        array<shared<Result>> hold; // Prevents result files from being recycled
        Dict args = copy(arguments); args.insert("source.cylinder"_);
        VBox vbox (Linear::Share, Linear::Expand);
        HList<Text> header;
        for(string result : (string[]){"name"_, "resolution"_, "voxelSize"_, "physicalSize"_}) header << Text(format(Bold)+process.getResult(result, args)->data);
        vbox << &header;
        UniformGrid<Item> slices;
        for(string target : (string[]){"source"_, "denoised"_, "thresholded"_, "distance"_, "skeleton"_, "maximum"_}) {
            shared<Result> result = process.getResult(target, args);
            Image slice = ::slice(toVolume(result),0.5f, true, true, true);
            if(target=="distance"_ || target=="skeleton"_ || target=="maximum"_) for(byte4& pixel: slice.buffer) { pixel=byte4(0xFF)-pixel; pixel.a=0xFF; } // Inverts color for print
            slices << Item(resize(slice,slice.size()/3), target, 16, true);
            hold << move(result);
        }
        vbox << &slices;
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
        output(outputs, "summary"_, "png"_, [&]{return encodePNG(renderToImage(&vbox, 2*int2(round(1024/sqrt(2.)), 1024), 1.5*96/*dpi*/));});
    }
};
