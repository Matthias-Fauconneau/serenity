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
struct Summary : Operation {
    string parameters() const override { return "path denoise"_; }
    void execute(const Dict& arguments, const Dict&, const ref<Result*>& outputs, const ref<const Result*>&, ResultManager& results) override {
        const int2 pageSize = int2(round(1024/sqrt(2.)), 1024);
        Dict args = copy(arguments);
        VBox vbox (Linear::Share, Linear::Expand);
        HList<Text> header;
        for(string result : (string[]){"name"_, "resolution"_, "voxelSize"_, "physicalSize"_}) header << Text(format(Bold)+results.getResult(result, args)->data);
        vbox << &header;
        UniformGrid<Item> slices;
        int scale=0;
        args.insert(String("z"_),0.5);
        for(auto target: map<string, string>({"png-denoised"_, "png-background"_, "png-connected"_, "png-maximum"_},{"Density"_,"Pore"_,"Skeleton"_,"Maximum"_})) {
            shared<Result> result = results.getResult(target.key, args);
            Image slice = decodeImage(result->data);
            assert_(slice);
            if(!scale) scale = max(2.,ceil(real(slice.size().x)/(2*pageSize.x/2)));
            slices << Item(resize(slice,slice.size()/scale), target.value, 16, true);
        }
        args.remove("z"_);
        vbox << &slices;
        output(outputs, "slices"_, "png"_, [&]{
            VBox vbox (Linear::Share, Linear::Expand); vbox<<&header<<&slices;
            return encodePNG(renderToImage(vbox, int2(2,1)*pageSize, 1.5*96/*dpi*/));
        });
        HList<Text> properties;
        properties << Text("Porosity: "_+format(Bold)+dec(round(parseScalar(results.getResult("porosity"_, args)->data)*100))+"%"_);
        real resolution = parseScalar(results.getResult("resolution"_, args)->data);
        for(auto result : map<string, string>({"critical-radius"_,"mean-radius"_,"representative-radius"_},{"Critical bottleneck radius"_,"Mean pore radius"_,"Representative radius"_})) {
            real length = parseScalar(results.getResult(result.key, args)->data);
            properties << Text(result.value+": "_+format(Bold)+dec(round(resolution*length))+" μm ("_+dec(round(length))+" vx)"_);
        }
        UniformGrid<Plot> plots;
        assert_(args.at("threshold"_)=="otsu"_);
        {Plot otsu; otsu.title = String("Interclass deviation versus threshold"_), otsu.xlabel=String("μ"_), otsu.ylabel=String("σ"_);
            otsu.legends << String("Radiodensity probability"_); otsu.dataSets << parseNonUniformSample(results.getResult("distribution-radiodensity"_, args)->data);
            otsu.legends << String("Interclass deviation (Otsu)"_); otsu.dataSets << parseNonUniformSample(results.getResult("otsu-interclass-deviation-normalized"_, args)->data);
            plots << move(otsu);}
        {Plot plot; plot.title = String("Pore size distribution"_), plot.xlabel=String("r [μm]"_), plot.ylabel=String("V"_);
            plot.legends << String("Probability density estimation"); plot.dataSets << parseNonUniformSample(results.getResult("distribution-radius-scaled"_, args)->data);
            plots << move(plot);}
        {Plot plot; plot.title = String("Representative elementary volume"_), plot.xlabel=String("R [μm]"_), plot.ylabel=String("ε"_);
            plot.legends << String("Pore size distribution deviation"); plot.dataSets << parseNonUniformSample(results.getResult("ε(R)"_, args)->data);
            plots << move(plot);}
        {Plot plot; plot.title = String("Bottleneck radius"_), plot.xlabel=String("r [μm]"_), plot.ylabel=String("V"_);
            plot.legends << String("Unconnected volume"); plot.dataSets << parseNonUniformSample(results.getResult("unconnected(λ)"_, args)->data);
            plot.legends << String("Connected volume"); plot.dataSets << parseNonUniformSample(results.getResult("connected(λ)"_, args)->data);
            plots << move(plot);}
        vbox << &plots;
        vbox << &properties;
        output(outputs, "plots"_, "png"_, [&]{
            VBox vbox (Linear::Share, Linear::Expand);
            vbox<<&plots<<&properties;
            return encodePNG(renderToImage(vbox, int2(2,1)*pageSize, 1.5*96/*dpi*/));
        });
        output(outputs, "summary"_, "png"_, [&]{ return encodePNG(renderToImage(vbox, 2*pageSize, 1.5*96/*dpi*/)); });
    }
};
template struct Interface<Operation>::Factory<Summary>;
