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
    void execute(const Dict& arguments, const ref<Result*>& outputs, const ref<Result*>&, ResultManager& results) override {
        array<shared<Result>> hold; // Prevents result files from being recycled
        Dict args = copy(arguments); args.insert("source.cylinder"_);
        VBox vbox (Linear::Share, Linear::Expand);
        HList<Text> header;
        for(string result : (string[]){"name"_, "resolution"_, "voxelSize"_, "physicalSize"_}) header << Text(results.getResult(result, args)->data);
        vbox << &header;
        UniformGrid<Item> slices;
        for(string target : (string[]){"source"_, "denoised"_, "colorize"_, "distance"_, "skeleton"_, "maximum"_}) {
            shared<Result> result = results.getResult(target, args);
            Image slice = ::slice(toVolume(result),0.5f, true, true, true);
            slices << Item(resize(slice,slice.size()/3), target, 16, true);
            hold << move(result);
        }
        vbox << &slices;
        UniformGrid<Plot> plots;
        assert_(args.at("threshold"_)=="otsu"_);
        {Plot otsu; otsu.title = String("Interclass deviation versus threshold"_), otsu.xlabel=String("μ"_), otsu.ylabel=String("σ"_);
            otsu.legends << String("Radiodensity probability"_); otsu.dataSets << parseNonUniformSample(results.getResult("distribution-radiodensity"_, args)->data);
            otsu.legends << String("Otsu inter-class deviation"_); otsu.dataSets << parseNonUniformSample(results.getResult("otsu-interclass-deviation"_, args)->data);
            plots << move(otsu);}
        {Plot PSD; PSD.title = String("Pore size distribution"_), PSD.xlabel=String("r"_), PSD.ylabel=String("V"_);
            PSD.legends << String("Density estimation"); PSD.dataSets << parseNonUniformSample(results.getResult("distribution-radius"_, args)->data);
            plots << move(PSD);}
        {Plot REV; REV.title = String("Representative elementary volume"_), REV.xlabel=String("R"_), REV.ylabel=String("ε"_);
            REV.legends << String("Pore size distribution deviation"); REV.dataSets << parseNonUniformSample(results.getResult("ε(R)"_, args)->data);
            plots << move(REV);}
        vbox << &plots;
        output(outputs, "summary"_, "png"_, [&]{return encodePNG(renderToImage(&vbox, 2*int2(round(1024/sqrt(2.)), 1024), 2*96/*dpi*/));});
    }
};
