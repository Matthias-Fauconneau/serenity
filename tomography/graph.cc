#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"
#include "graphics.h"
#include "plot.h"
#include "png.h"

struct PlotView : Plot {
    Dict dimensionLabels = parseDict("rotationCount:Rotations,photonCount:Photons,projectionCount:Projections,subsetSize:per subset"_);
    Dict coordinateLabels = parseDict("single:Single,double:Double,adaptive:Adaptive"_);
    Dict valueLabels = parseDict("Global NMSE %:Total NMSE %,Center NMSE %:Central NMSE %"_); // = parseDict("Iterations count:Iterations,Center NMSE %:Central,Central NMSE %:Central,Extreme NMSE %:Extreme,Total NMSE %:Total,Global NMSE %:Total,Time (s):Time"_);
    array<String> valueNames;
    string xName = "Time (s)"_, yName;

    PlotView(string xName, string yName) : xName(xName), yName(yName) {
        const bool dB = true;
        Plot::xLabel = String(xName); Plot::yLabel = dB ? section(yName,' ')+" dB"_ : String(yName);
        if(!dB) Plot::log[1] = true;
        Folder results = "Results"_;
        for(string name: results.list()) {
            if(name.contains('.')) continue;
            Dict configuration = parseDict(name);
            //if(configuration["method"_]=="SART"_ ) continue;
            if(configuration["trajectory"_]=="adaptive"_ && (int(configuration.at("rotationCount"_))==1||int(configuration.at("rotationCount"_))==4)) continue; // Skips 1,4 adaptive rotations (only 2,3,5 is relevant)
            if(configuration["trajectory"_]=="adaptive"_) configuration.at("rotationCount"_) = int(configuration["rotationCount"_])-1; // Converts adaptive total rotation count to helical rotation count
            for(auto& dimension: configuration.keys) { // Converts dimension identifiers to labels
                if(dimensionLabels.contains(dimension)) dimension = copy(dimensionLabels.at(dimension));
                else { // Converts CamelCase identifier to user-facing labels
                    String label; for(uint i: range(dimension.size)) {
                        char c=dimension[i];
                        if(i==0) label << toUpper(c);
                        else {
                            if(isUpper(c) && (!label || label.last()!=' ')) label << ' ';
                            label << toLower(c);
                        }
                    }
                    dimension = move(label);
                }
            }
            for(auto& coordinate: configuration.values) if(coordinateLabels.contains(coordinate)) coordinate=copy(coordinateLabels.at(coordinate)); // Converts coordinate identifiers to labels

            DataSet dataSet;
            dataSet.label = str(configuration);
            String result = readFile(name, results);
            for(string line: split(result, '\n')) {
                Dict values;
                if(startsWith(line, "{"_)) values = parseDict(line);
                else {// Backward compatibility (REMOVEME)
                    TextData s (line);
                    const int subsetSize = configuration.at("per subset"_);
                    values["Iterations"_] = subsetSize*(s.integer()+1); s.skip(" "_);
                    values["Central NMSE %"_] = s.decimal(); s.skip(" "_);
                    values["Extreme NMSE %"_] = s.decimal(); s.skip(" "_);
                    values["Total NMSE %"_] = s.decimal(); s.skip(" "_);
                    values["SNR"_] = -s.decimal(); s.skip(" "_); /*Negates as best is maximum*/
                    values["Time (s)"_] = s.decimal();
                    assert_(!s, s.untilEnd(), "|", line);
                }
                for(auto& valueName: values.keys) if(valueLabels.contains(valueName)) valueName = copy(valueLabels.at(valueName));
                for(const String& valueName: values.keys) if(!valueNames.contains(valueName)) valueNames << copy(valueName);
                float value = values.at(yName);
                if(dB) value = -10*log10(value/100);
                dataSet.data.insert(values.at(xName), value);
            }
            //points.insert(move(configuration), move(dataSet));
            dataSets << move(dataSet);
        }
    }
    int2 sizeHint() override { return -int2(2560, clip(1385, (int)dataSets.size*8, 2560)); }
};

// -> file.cc
#include <sys/inotify.h>
/// Watches a folder for new files
struct FileWatcher : File, Poll {
    FileWatcher(string path, function<void(string)> fileCreated, function<void(string)> fileDeleted={})
        : File(inotify_init1(IN_CLOEXEC)), Poll(File::fd), watch(check(inotify_add_watch(File::fd, strz(path), IN_CREATE|IN_DELETE))),
          fileCreated(fileCreated), fileDeleted(fileDeleted) {}
    void event() override {
        while(poll()) {
            //::buffer<byte> buffer = readUpTo(2*sizeof(inotify_event)+4); // Maximum size fitting only a single event (FIXME)
            ::buffer<byte> buffer = readUpTo(sizeof(struct inotify_event) + 256);
            inotify_event e = *(inotify_event*)buffer.data;
            string name = str((const char*)buffer.slice(__builtin_offsetof(inotify_event, name), e.len-1).data);
            if((e.mask&IN_CREATE) && fileCreated) fileCreated(name);
            if((e.mask&IN_DELETE) && fileDeleted) fileDeleted(name);
        }
    }
    const uint watch;
    function<void(string)> fileCreated;
    function<void(string)> fileDeleted;
};

struct Application {
    PlotView view { "Time (s)"_, arguments()?arguments()[0]:"Total NMSE %"_};
    Window window {&view, "Results"_};
    FileWatcher watcher{"Results"_, [this](string){ view=PlotView(view.xName, view.yName);/*Reloads*/ window.render(); } };
    Application() {
        for(string yName: {"Central NMSE %"_,"Extreme NMSE %"_,"Total NMSE %"_}) {
            PlotView view ("Time (s)"_, yName);
            Image image ( abs(view.sizeHint()) );
            assert_( image.size() < int2(16384), view.sizeHint());
            fill(image, Rect(image.size()), white);
            view.Widget::render( image );
            writeFile("Plot "_+replace(view.yLabel," %"_,""_), encodePNG(image));
        }
        window.setSize(-1);
        window.show();
    }
} app;
