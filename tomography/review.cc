#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"
#include "graphics.h"
#include "png.h"

struct ArrayView : Widget {
    Folder results = "Results"_;
    array<String> names = results.list(); // Kept for references
    array<string> parameters = split("Size,Grain Radius,Resolution,Trajectory,Rotations,Photons,Projections"_,',');
    string valueName;
    string bestName = valueName=="k"_? "NMSE"_ : valueName;
    array<array<Variant>> values;
    map<array<String>, Variant> points;
    float min = inf, max = -inf;
    uint textSize;

    ArrayView(string valueName, uint textSize=16) : valueName(valueName), textSize(textSize) {
        values.grow(parameters.size);
        values[parameters.indexOf("Trajectory"_)] << Variant(String("single"_)) << Variant(String("double"_)) << Variant(String("adaptive"_)); // Force this specific order
        for(string name: names) {
            if(name.contains('.')) continue;
            array<String> arguments = apply(split(name), [](string s){return String(s);});
            if(arguments[parameters.indexOf("Trajectory"_)]=="adaptive"_) arguments[parameters.indexOf("Rotations"_)] = str(fromInteger(arguments[parameters.indexOf("Rotations"_)])-1); // Converts adaptive total rotation count to helical rotation count
            arguments.pop(); // Ignores subset size
            assert_(arguments.size == parameters.size, parameters, arguments);
            for(uint i: range(parameters.size)) if(!values[i].contains(arguments[i])) values[i].insertSorted(Variant(String(string(arguments[i]))));
            float best = inf; Variant value;
            for(TextData s = readFile(name, results);s;) {
                map<string, Variant> values;
                values["k"_] = s.integer()+1; s.skip(" "_);
                values["Central"_] = s.decimal(); s.skip(" "_);
                values["Extreme"_] = s.decimal(); s.skip(" "_);
                values["NMSE"_] = s.decimal(); s.skip(" "_);
                values["SNR"_] = -s.decimal(); /*Negates as best is maximum*/ s.skip("\n"_);
                if((float)values[bestName] < best) {
                    best = values[bestName];
                    value = move(values[valueName]);
                }
            }
            min = ::min(min, (float)value), max = ::max(max, (float)value);
            points.insert(move(arguments), move(value));
        }
    }
    array<string> dimensions[2] = {split("Trajectory Photons"_),split("Rotations Projections"_)};
    /// Returns number of cells for \a axis at \a level
    int cellCount(uint axis, uint level=0) {
        int cellCount = 1;
        for(string parameter: dimensions[axis].slice(level)) {
            assert_(parameters.contains(parameter));
            cellCount *= values[parameters.indexOf(parameter)].size;
        }
        return cellCount;
    }
    int2 cellCount() { return int2(cellCount(0),cellCount(1)); }
    int2 levelCount() { return int2(dimensions[0].size,dimensions[1].size); }
    int2 sizeHint() { return (levelCount().yx()+int2(1)+cellCount()) * int2(5*textSize); }
    void render() override {
        assert_(cellCount(), cellCount());
        int2 cellSize = target.size() / (levelCount().yx()+int2(1)+cellCount());
        // Fixed parameters in unused top-left corner
        String fixed;
        for(uint i: range(parameters.size)) if(values[i].size==1) fixed << parameters[i]+": "_+str(values[i])+"\n"_;
        Text(fixed, textSize).render(clip(target, Rect(int2(dimensions[1].size,dimensions[0].size)*cellSize)));
        // Value name in unused top-left cell
        Text(format(TextFormat::Bold)+valueName, textSize).render(clip(target, int2(dimensions[1].size,dimensions[0].size)*cellSize+Rect(cellSize)));
        // Content
        for(uint X: range(cellCount(0))) {
            for(uint Y: range(cellCount(1))) {
                map<string, string> arguments; // Argument values corresponding to current cell
                for(int axis: range(2)) {
                    uint x = ref<uint>({X,Y})[axis];
                    for(uint level: range(dimensions[axis].size)) {
                        string parameter = dimensions[axis][level];
                        uint index = x / cellCount(axis, level+1);
                        arguments[parameter] = values[parameters.indexOf(parameter)][index];
                        x = x % cellCount(axis, level+1);
                    }
                }
                array<String> key;
                for(uint i: range(parameters.size)) {
                    string parameter = parameters[i];
                    if(arguments.contains(parameter)) key << String(arguments[parameter]);
                    else {
                        assert_(values[i].size == 1, "Multiple values for",parameter,":",values[i]);
                        key << String(string(values[i][0]));
                    }
                }
                if(points.contains(key)) {
                    Image cell = clip(target, ((levelCount().yx()+int2(1)+int2(X,Y)) * cellSize)+Rect(cellSize));
                    float value = points.at(key);
                    float v = (value-min)/(max-min);
                    fill(cell, Rect(cell.size()), vec3(0,1-v,v));
                    float realValue = abs(value); // Values where maximum is best have been negated
                    Text((value==min?format(Bold):""_)+(points.at(key).isInteger?dec(realValue):ftoa(realValue)),round(textSize*(1+(1-v))),black).render(cell);
                }
            }
        }
        // Headers (and lines over fills)
        for(uint axis: range(2)) {
            uint X = 1;
            for(uint level: range(dimensions[axis].size)) {
                int2 origin = int2(dimensions[!axis].size-1+1, level);
                if(axis) origin=origin.yx();
                Text(dimensions[axis][level],textSize,black).render(clip(target, (origin*cellSize)+Rect(cellSize)));

                uint parameter = parameters.indexOf(dimensions[axis][level]);
                uint size = values[parameter].size;
                for(uint x: range(X)) {
                    for(uint index: range(size)) {
                        int2 origin = int2(dimensions[!axis].size +1 + x*size + index*cellCount(axis,level+1), level);
                        int2 size = int2(cellCount(axis,level+1), 1);
                        if(axis) origin=origin.yx(), size=size.yx();
                        origin *= cellSize;
                        if(level<dimensions[axis].size-1) {
                            if(!axis) fill(target, Rect(origin,int2(origin.x+1, target.size().y)));
                            if(axis) fill(target, Rect(origin,int2(target.size().x,origin.y+1)));
                        }
                        Text(values[parameter][index],textSize,black).render(clip(target, origin+Rect(size*cellSize)));
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
    ArrayView view {"NMSE"_};
    Window window {&view, "Results"_};
    FileWatcher watcher{"Results"_, [this](string){ view=ArrayView(view.valueName);/*Reloads*/ window.render(); } };
    Application() {
        for(string valueName: {"NMSE"_,"Central"_,"Extreme"_,"SNR"_,"k"_}) {
            ArrayView view (valueName, 32);
            Image image ( view.sizeHint() );
            fill(image, Rect(image.size()), white);
            view.Widget::render( image );
            writeFile(valueName, encodePNG(image));
        }
        window.setSize(-1);
        window.show();
    }
} app;

