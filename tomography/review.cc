#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"
#include "graphics.h"
#include "png.h"

struct ArrayView : Widget {
    Dict dimensionLabels = parseDict("rotationCount:Rotations,photonCount:Photons,projectionCount:Projections,subsetSize:per subset"_);
    Dict coordinateLabels = parseDict("single:\0Single,double:\1Double,adaptive:\2Adaptive"_); // Sorts
    Dict valueLabels = parseDict("Iterations count:Iterations,Center NMSE %:Central,Central NMSE %:Central,Extreme NMSE %:Extreme,Total NMSE %:Total,Global NMSE %:Total,Time (s):Time"_);
    string valueName;
    string bestName = (valueName=="Iterations"_ || valueName == "Time"_) ? "Total"_ : valueName;
    array<String> valueNames;
    map<Dict, Variant> points; // Data points
    float min = inf, max = -inf;
    uint textSize;

    ArrayView(string valueName, uint textSize=16) : valueName(valueName), textSize(textSize) {
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
            points.insert(move(configuration), move(value));
        }
        min = ::min(points.values);
        if(0) {
            array<Variant> values = copy(points.values);
            for(uint _unused i: range(2)) values.remove(::max(values)); // Removes 2 maximum values before computing maximum bound (discards outliers)
            max = ::max(values);
        } else { // Ignores SART values for maximum bound
            array<Variant> values = filterIndex(points.values, [this](size_t i){ return points.keys[i].at("Method"_)=="SART"_ || (this->valueName=="Time"_ && points.keys[i].at("Method"_)=="MLTR"_ && points.keys[i].at("per subset"_)==points.keys[i].at("Projections"_) ); });
            max = ::max(values);
        }
    }
    array<string> dimensions[2] = {split("Trajectory,Photons,Method"_,','),split("Rotations,Projections,per subset"_,',')};

    /// Returns coordinates along \a dimension occuring in points matching \a filter
    array<Variant> coordinates(string dimension, const Dict& filter) const {
        array<Variant> allCoordinates;
        for(const Dict& coordinates: points.keys) if(coordinates.includes(filter)) {
            assert_(coordinates.contains(dimension), coordinates, dimension);
            if(!allCoordinates.contains(coordinates.at(dimension))) allCoordinates.insertSorted(copy(coordinates.at(dimension)));
        }
        return allCoordinates;
    }
    /// Returns coordinates occuring in \a points
    map<String, array<Variant>> coordinates(const map<Dict, Variant>& points) const {
        map<String, array<Variant>> allCoordinates;
        for(const Dict& coordinates: points.keys) for(const auto coordinate: coordinates)
            if(!allCoordinates[copy(coordinate.key)].contains(coordinate.value)) allCoordinates.at(coordinate.key).insertSorted(copy(coordinate.value));
        return allCoordinates;
    }
    /// Returns number of cells for the given \a axis, \a level and \a coordinates
    int cellCount(uint axis, uint level, Dict& filter) const {
        if(level == dimensions[axis].size) return 1;
        string dimension = dimensions[axis][level];
        int cellCount = 0;
        assert_(!filter.contains(dimension));
        for(const Variant& coordinate: coordinates(dimension, filter)) {
            filter[String(dimension)] = copy(coordinate);
            cellCount += this->cellCount(axis, level+1, filter);
        }
        filter.remove(dimension);
        return cellCount;
    }
    int cellCount(uint axis, uint level=0) const { Dict filter; return cellCount(axis, level, filter); }
    int2 cellCount() { return int2(cellCount(0),cellCount(1)); }
    int2 levelCount() { return int2(dimensions[0].size,dimensions[1].size); }
    int2 sizeHint() { return (levelCount().yx()+int2(1)+cellCount()) * int2(80*textSize/16,32*textSize/16); }

    uint renderHeader(int2 cellSize, uint axis, uint level, Dict& filter, uint offset=0) {
        if(level==dimensions[axis].size) return 1;
        string dimension = dimensions[axis][level];
        assert_(!filter.contains(dimension));
        uint cellCount = 0;
        for(const Variant& coordinate: coordinates(dimension, filter)) {
            filter[String(dimension)] = copy(coordinate);
            uint childCellCount = renderHeader(cellSize, axis, level+1, filter, offset+cellCount);
            int2 origin = int2(dimensions[!axis].size+1+offset+cellCount, level);
            int2 size = int2(childCellCount, 1);
            if(axis) origin=origin.yx(), size=size.yx();
            origin *= cellSize;
            if(level<dimensions[axis].size-1) {
                int width = dimensions[axis].size-1-level;
                if(!axis) fill(target, Rect(origin+int2(-width/2,0),int2(origin.x+(width+1)/2, target.size().y)));
                if(axis) fill(target, Rect(origin+int2(0,-width/2),int2(target.size().x,origin.y+(width+1)/2)));
            }
            String label = copy(coordinate);
            if(label[0] < 16) label.removeAt(0); // Removes sort key
            Text(label, textSize, black).render(clip(target, origin+Rect(size*cellSize)));
            cellCount += childCellCount;
        }
        filter.remove(dimension);
        return cellCount;
    }

    int renderCell(int2 cellSize, uint axis, uint level, Dict& filterX, Dict& filterY, int2 origin=0) {
        if(level==dimensions[axis].size) {
            if(axis==0) { renderCell(cellSize, 1, 0, filterX, filterY, origin); } // Descends dimensions tree on other array axis
            else { // Renders cell
                for(const Dict& coordinates: points.keys) if(coordinates.includes(filterX) && coordinates.includes(filterY)) {
                    Image cell = clip(target, ((levelCount().yx()+int2(1)+origin) * cellSize)+Rect(cellSize));
                    const Variant& point = points.at(coordinates);
                    float value = point;
                    float v = (value-min)/(max-min);
                    fill(cell, Rect(cell.size()), vec3(0,1-v,v));
                    float realValue = abs(value); // Values where maximum is best have been negated
                    String text = (value==0?format(Bold):""_)+(point.isInteger?dec(realValue):ftoa(realValue));
                    Text(text, ::max(16,(int)round(textSize*(1+(1-v)))),black).render(cell);
                    break;
                }
            }
            return 1;
        }
        string dimension = dimensions[axis][level];
        Dict& filter = axis ? filterY : filterX;
        assert_(!filter.contains(dimension));
        int offset = 0;
        for(const Variant& coordinate: coordinates(dimension, filter)) {
            filter[String(dimension)] = copy(coordinate);
            int childCellCount = renderCell(cellSize, axis, level+1, filterX, filterY, origin+int2(axis?0:offset,axis?offset:0));
            offset += childCellCount;
        }
        filter.remove(dimension);
        return offset;
    }

    void render() override {
        assert_(cellCount(), cellCount());
        int2 cellSize = target.size() / (levelCount().yx()+int2(1)+cellCount());
        // Fixed coordinates in unused top-left corner
        String fixed;
        for(const auto& coordinate: coordinates(points)) if(coordinate.value.size==1) fixed << coordinate.key+": "_+str(coordinate.value)+"\n"_;
        Text(fixed, textSize).render(clip(target, Rect(int2(dimensions[1].size,dimensions[0].size)*cellSize)));
        // Value name in unused top-left cell
        Text(format(TextFormat::Bold)+valueName, textSize).render(clip(target, int2(dimensions[1].size,dimensions[0].size)*cellSize+Rect(cellSize)));
        // Dimensions
        for(uint axis: range(2)) for(uint level: range(dimensions[axis].size)) {
            int2 origin = int2(dimensions[!axis].size-1+1, level);
            if(axis) origin=origin.yx();

            string dimension = dimensions[axis][level];
            Text(dimension,textSize,black).render(clip(target, (origin*cellSize)+Rect(cellSize)));
        }

        // Content
        Dict filterX, filterY; renderCell(cellSize, 0, 0, filterX, filterY);

        // Headers (and lines over fills)
        for(uint axis: range(2)) { Dict filter; renderHeader(cellSize, axis, 0, filter); }
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
        for(string valueName: {"Central"_,"Extreme"_,"Total"_,"Time"_}) {
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
