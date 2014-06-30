#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"

struct Application : Widget {
    Folder results = "Results"_;
    array<String> names = results.list(); // Kept for references
    map<string, array<Variant>> values;
    map<array<string>, float/*Variant*/> points;

    Window window {this, "Results"_};
    Application() {
        for(string name: names) {
            if(name.contains('.')) continue;
            array<string> arguments = split(name);
            ref<string> parameters = {"Size"_,"Grain Radius"_,"Resolution"_,"Trajectory"_,"Rotations"_,"Photons"_,"Projections"_,"Subsets size"_};
            for(uint i: range(parameters.size)) if(!values[parameters[i]].contains(arguments[i])) values[parameters[i]].insertSorted(Variant(String(arguments[i])));
            float bestTotalNMSE = inf;
            for(TextData s = readFile(name, results);s;) {
                uint _unused k = s.integer(); s.skip(" "_);
                float _unused centerNMSE = s.decimal(); s.skip(" "_);
                float _unused extremeNMSE = s.decimal(); s.skip(" "_);
                float totalNMSE = s.decimal(); s.skip(" "_);
                float _unused SNR = s.decimal(); s.skip("\n"_);
                bestTotalNMSE = min(bestTotalNMSE, totalNMSE);
            }
            points.insert(move(arguments), bestTotalNMSE);
        }
        log(values);
        window.setSize(-1);
        window.show();
    }
    array<string> parameters[2] = {split("Trajectory Photons"_),split("Rotations Projections"_)};
    /// Returns number of cells for \a axis at \a level
    int cellCount(uint axis, uint level=0) {
        int cellCount = 1;
        for(string parameter: parameters[axis].slice(level)) cellCount *= values[parameter].size;
        return cellCount;
    }
    int2 cellCount() { return int2(cellCount(0),cellCount(1)); }
    int2 levelCount() { return int2(parameters[0].size,parameters[1].size); }
    int2 sizeHint() { return (levelCount().yx()+int2(1)+cellCount()) * int2(64); }
    void render() override {
        assert_(cellCount(), cellCount());
        int2 cellSize = target.size() / (levelCount().yx()+int2(1)+cellCount());
        for(uint axis: range(2)) {
            uint X = 1;
            for(uint level: range(parameters[axis].size)) {
                int2 origin = int2(parameters[!axis].size-1+1, level);
                if(axis) origin=origin.yx();
                Text(parameters[axis][level]).render(clip(target, (origin*cellSize)+Rect(cellSize)));

                uint size = values[parameters[axis][level]].size;
                for(uint x: range(X)) {
                    for(uint index: range(size)) {
                        int2 origin = int2(parameters[!axis].size +1 + x*size + index*cellCount(axis,level+1), level);
                        int2 size = int2(cellCount(axis,level+1), 1);
                        if(axis) origin=origin.yx(), size=size.yx();
                        Text(values[parameters[axis][level]][index]).render(clip(target, (origin*cellSize)+Rect(size*cellSize)));
                    }
                }
                X *= size;
            }
        }
        for(uint X: range(cellCount(0))) {
            for(uint Y: range(cellCount(1))) {
                map<string, string> arguments; // Argument values corresponding to current cell
                for(int axis: range(2)) {
                    uint x = ref<uint>({X,Y})[axis];
                    for(uint level: range(parameters[axis].size)) {
                        string parameter = parameters[axis][level];
                        uint index = x / cellCount(axis, level+1);
                        arguments[parameter] = values[parameter][index];
                        x = x % cellCount(axis, level+1);
                    }
                }
                array<string> key;
                for(uint i: range(values.size())) {
                    string parameter = values.keys[i];
                    if(arguments.contains(parameter)) key << arguments[parameter];
                    else {
                        assert_(values.values[i].size == 1, "Multiple values for",parameter,":",values.values[i]);
                        key << values.values[i][0];
                    }
                }
                Text(str(points.contains(key)?points.at(key):0)).render(clip(target, ((levelCount().yx()+int2(1)+int2(X,Y)) * cellSize)+Rect(cellSize)));
            }
        }
    }
} app;
