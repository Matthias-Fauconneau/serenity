#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"
#include "graphics.h"
#include "png.h"

struct ArrayView : Widget {
    Dict parameterLabels = parseDict("rotationCount:Rotations,photonCount:Photons,projectionCount:Projections,subsetSize:per subset"_);
    Dict valueLabels = parseDict("Iterations count:Iterations,Center NMSE %:Central,Central NMSE %:Central,Extreme NMSE %:Extreme,Total NMSE %:Total,Global NMSE %:Total,Time (s):Time"_);
    string valueName;
    string bestName = (valueName=="Iterations"_ || valueName == "Time"_) ? "Total"_ : valueName;
    map<String, array<Variant>> arguments; // Arguments occuring for each parameter
    array<String> valueNames;
    map<Dict, Variant> points; // Data points
    float min = inf, max = -inf;
    uint textSize;

    ArrayView(string valueName, uint textSize=16) : valueName(valueName), textSize(textSize) {
        arguments["Trajectory"_] << Variant(String("Single"_)) << Variant(String("Double"_)) << Variant(String("Adaptive"_)); // Force this specific order
        Folder results = "Results"_;
        for(string name: results.list()) {
            if(name.contains('.')) continue;
            Dict configuration = parseDict(name);
            for(auto& parameter: configuration.keys) {
                if(parameterLabels.contains(parameter)) parameter = copy(parameterLabels.at(parameter));
                else { // Converts CamelCase identifier to user-facing labels
                    String label; for(uint i: range(parameter.size)) {
                        char c=parameter[i];
                        if(i==0) label << toUpper(c);
                        else {
                            if(isUpper(c) && (!label || label.last()!=' ')) label << ' ';
                            label << toLower(c);
                        }
                    }
                    parameter = move(label);
                }
            }
            for(auto& argument: configuration.values) argument[0]=toUpper(argument[0]); // Converts arguments to user-facing labels
            if(configuration["Trajectory"_]=="Adaptive"_) configuration.at("Rotations"_) = int(configuration["Rotations"_])-1; // Converts adaptive total rotation count to helical rotation count

            float best = inf; Variant value;
            String result = readFile(name, results);
            for(string line: split(result, '\n')) {
                Dict values;
                if(startsWith(line, "{"_)) values = parseDict(line);
                else {// Backward compatibility (REMOVEME)
                    TextData s (line);
                    const int subsetSize = configuration.at("per subset"_);
                    values["Iterations"_] = subsetSize*(s.integer()+1); s.skip(" "_);
                    values["Central"_] = s.decimal(); s.skip(" "_);
                    values["Extreme"_] = s.decimal(); s.skip(" "_);
                    values["Total"_] = s.decimal(); s.skip(" "_);
                    values["SNR"_] = -s.decimal(); s.skip(" "_); /*Negates as best is maximum*/
                    values["Time (s)"_] = s.decimal();
                    assert_(!s, s.untilEnd(), "|", line);
                }
                for(auto& valueName: values.keys) if(valueLabels.contains(valueName)) valueName = copy(valueLabels.at(valueName));
                for(const String& valueName: values.keys) valueNames += copy(valueName);
                if(float(values.at(bestName)) < best) {
                    best = values[bestName];
                    value = move(values.at(valueName));
                }
            }
            min = ::min(min, (float)value), max = ::max(max, (float)value);
            for(const auto argument: (const Dict&)configuration) if(!arguments[copy(argument.key)].contains(argument.value)) arguments.at(argument.key).insertSorted(copy(argument.value));
            points.insert(move(configuration), move(value));
        }
    }
    array<string> dimensions[2] = {split("Trajectory,Photons,Method"_,','),split("Rotations,Projections,per subset"_,',')};
    /// Returns number of cells for \a axis at \a level
    int cellCount(uint axis, uint level=0) {
        int cellCount = 1;
        for(string parameter: dimensions[axis].slice(level)) cellCount *= arguments.at(parameter).size;
        return cellCount;
    }
    int2 cellCount() { return int2(cellCount(0),cellCount(1)); }
    int2 levelCount() { return int2(dimensions[0].size,dimensions[1].size); }
    int2 sizeHint() { return (levelCount().yx()+int2(1)+cellCount()) * int2(80*textSize/16,24*textSize/16); }
    void render() override {
        assert_(cellCount(), cellCount(), arguments);
        int2 cellSize = target.size() / (levelCount().yx()+int2(1)+cellCount());
        // Fixed parameters in unused top-left corner
        String fixed;
        for(const auto& argument: arguments) if(argument.value.size==1) fixed << argument.key+": "_+str(argument.value)+"\n"_;
        Text(fixed, textSize).render(clip(target, Rect(int2(dimensions[1].size,dimensions[0].size)*cellSize)));
        // Value name in unused top-left cell
        Text(format(TextFormat::Bold)+valueName, textSize).render(clip(target, int2(dimensions[1].size,dimensions[0].size)*cellSize+Rect(cellSize)));
        // Content
        for(uint X: range(cellCount(0))) {
            for(uint Y: range(cellCount(1))) {
                Dict coordinates; // Argument values corresponding to current cell (i.e point coordinates)
                for(int axis: range(2)) { // Coordinates from cell
                    uint x = ref<uint>({X,Y})[axis];
                    for(uint level: range(dimensions[axis].size)) {
                        string parameter = dimensions[axis][level];
                        uint index = x / cellCount(axis, level+1);
                        coordinates.insertSorted(parameter, copy(arguments.at(parameter)[index]));
                        x = x % cellCount(axis, level+1);
                    }
                }
                for(const auto& argument: arguments) { // Fills in remaining fixed coordinates
                    if(!coordinates.contains(argument.key)) {
                        assert_(argument.value.size == 1, "Ambigous cell accepts multiple values for",argument.key,", got",argument.value);
                        coordinates.insertSorted(copy(argument.key), copy(argument.value[0]));
                    }
                }
                if(points.contains(coordinates)) {
                    Image cell = clip(target, ((levelCount().yx()+int2(1)+int2(X,Y)) * cellSize)+Rect(cellSize));
                    const Variant& point = points.at(coordinates);
                    float value = point;
                    float v = (value-min)/(max-min);
                    fill(cell, Rect(cell.size()), vec3(0,1-v,v));
                    float realValue = abs(value); // Values where maximum is best have been negated
                    Text((value==0?format(Bold):""_)+(point.isInteger?dec(realValue):ftoa(realValue)),round(textSize*(1+(1-v))),black).render(cell);
                }
            }
        }
        // Headers (and lines over fills)
        for(uint axis: range(2)) {
            uint X = 1;
            for(uint level: range(dimensions[axis].size)) {
                int2 origin = int2(dimensions[!axis].size-1+1, level);
                if(axis) origin=origin.yx();

                string parameter = dimensions[axis][level];
                Text(parameter,textSize,black).render(clip(target, (origin*cellSize)+Rect(cellSize)));

                uint size = arguments[parameter].size;
                for(uint x: range(X)) {
                    for(uint index: range(size)) {
                        int2 origin = int2(dimensions[!axis].size +1 + (x*size + index)*cellCount(axis,level+1), level);
                        int2 size = int2(cellCount(axis,level+1), 1);
                        if(axis) origin=origin.yx(), size=size.yx();
                        origin *= cellSize;
                        if(level<dimensions[axis].size-1) {
                            if(!axis) fill(target, Rect(origin,int2(origin.x+1, target.size().y)));
                            if(axis) fill(target, Rect(origin,int2(target.size().x,origin.y+1)));
                        }
                        Text(arguments[parameter][index], textSize, black).render(clip(target, origin+Rect(size*cellSize)));
                    }
                }
                X *= size;
            }
        }
    }
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
    ArrayView view { arguments()?arguments()[0]:"Total"_ };
    Window window {&view, "Results"_};
    FileWatcher watcher{"Results"_, [this](string){ view=ArrayView(view.valueName);/*Reloads*/ window.render(); } };
    Application() {
        for(string valueName: view.valueNames) {
            ArrayView view (valueName, 32);
            Image image ( view.sizeHint() );
            assert_( image.size() < int2(16384), view.sizeHint(), view.levelCount(), view.cellCount());
            fill(image, Rect(image.size()), white);
            view.Widget::render( image );
            writeFile(valueName, encodePNG(image));
        }
        window.setSize(-1);
        window.show();
    }
} app;
