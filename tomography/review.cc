#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"
#include "graphics.h"
#include "png.h"

static const bool smallestSubsets = false; // Filters subsetSize to only keep configurations with the smallest subsets (fastest convergence)
static const bool limitAcquisitionTime = false; // Filters configurations with Projections·Photons over acquisition time limit

//TODO: merge ArrayView and SliceArrayView
struct ArrayView : Widget {
    Dict dimensionLabels = parseDict("rotationCount:Revolutions,photonCount:Photons,projectionCount:Projections,subsetSize:per subset"_);
    Dict coordinateLabels = parseDict("single:\0Single,double:\1Double,adaptive:\2Adaptive"_); // Sorts
    Dict valueLabels = parseDict("Iterations count:Iterations,Central NMSE %:MSE_C,Extreme NMSE %:MSE_E,Total NMSE %:MSE_T,Center NMSE %:MSE_C,Global NMSE %:MSE_T,Normalized NMSE %:Normalized NMSE"_); // FIXME: Center, Global kept for backward comptability
    string valueName;
    string bestName = (valueName=="Iterations"_ || valueName == "Time (s)"_) ? "MSE_T"_ : valueName;
    array<String> valueNames;
    map<Dict, Variant> points; // Data points
    float min = inf, max = -inf;
    uint textSize;
    int2 headerCellSize = int2(80*textSize/16, textSize);
    int2 contentCellSize = int2(48*textSize/16, textSize);

    ArrayView(string valueName, const map<string, Variant>& parameters, uint textSize=16) : valueName(valueName), textSize(textSize) {
        Folder results = "Results"_;
        for(string fileName: results.list()) {
            string name = fileName;
            /**/  if(valueName=="SNR"_ || valueName=="SNR (dB)"_) { if(!endsWith(name, ".snr"_)) continue; name=section(name,'.',0,-2); }
            else if(valueName=="Normalized NMSE"_) { if(!endsWith(name, ".nmse"_)) continue; name=section(name,'.',0,-2); }
            else if(endsWith(name,".best"_) || endsWith(name,".snr"_) || endsWith(name,".nmse"_) || endsWith(name,".32"_) || endsWith(name,".128"_)) continue;
            String result = readFile(fileName, results);
            if(!result) { log("Empty result file", name); continue; }
            Dict configuration = parseDict(name);
            if(!configuration["photonCount"_].size) continue;// configuration["photonCount"_] = 0;
            if(configuration["trajectory"_]=="adaptive"_ && (int(configuration.at("rotationCount"_))==1||int(configuration.at("rotationCount"_))==4)) continue; // Skips 1,4 adaptive rotations (only 2,3,5 is relevant)
            if(configuration["trajectory"_]=="adaptive"_) configuration.at("rotationCount"_) = float(configuration["rotationCount"_])-1; // Converts adaptive total rotation count to helical rotation count
            if((float)configuration.at("rotationCount"_)==round((float)configuration.at("rotationCount"_))) configuration.at("rotationCount"_) = int(configuration.at("rotationCount"_));
            bool skip = false;
            for(const auto& coordinate: configuration) if(parameters.contains(coordinate.key) && !split(parameters.at(coordinate.key),',').contains(coordinate.value)) skip=true;/*continue 2;*/ // Skips missing values for explicitly specified dimensions
            if(skip) continue;
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
            for(auto& coordinate: configuration.values) assert_(coordinate.size, configuration);

            float best = inf; Variant value; //bool oldStyleFile = false;
            for(string line: split(result, '\n')) {
                Dict values;
                if(startsWith(line, "{"_)) {
                    if(!endsWith(line,"}"_)) continue; // Truncated file (I/O error)
                    assert_(endsWith(line,"}"_), line);
                    values = parseDict(line);
                } else {// Backward compatibility (REMOVEME)
                    //if(!oldStyleFile) { log("Old file", name, line); oldStyleFile=true; }
                    TextData s (line);
                    const int subsetSize = configuration.at("per subset"_);
                    values["Iterations"_] = s.integer()+1; s.skip(" "_);
                    values["Iterations·Projection/Subsets"_] = subsetSize*(int)values["Iterations"_];
                    values["Central NMSE %"_] = s.decimal(); s.skip(" "_);
                    values["Extreme NMSE %"_] = s.decimal(); s.skip(" "_);
                    values["Total NMSE %"_] = s.decimal(); s.skip(" "_);
                    values["SNR"_] = s.decimal(); s.skip(" "_);
                    values["Time (s)"_] = s.decimal();
                    assert_(!s, s.untilEnd(), "|", line);
                }
                if(values.contains("SNR"_)) values.insert(String("SNR (dB)"_), -10*log10(values.at("SNR"_))); //Converts to decibels, Negates as best is maximum
                for(auto& valueName: values.keys) if(valueLabels.contains(valueName)) valueName = copy(valueLabels.at(valueName));
                for(const String& valueName: values.keys) valueNames += copy(valueName);
                assert_(values.contains(bestName) && values.contains(valueName), bestName, valueName, values, name);
                if(values.contains("Iterations"_)) {
                    values.at("Iterations"_).isInteger = true;
                    if(File(fileName, results).modifiedTime() < (int64)Date(9,7,2014,16,00,00)*1000000000ll) { //FIXME: "Iterations" of evaluation ran before 07/09 16:00 is actually index, needs +1
                        values.at("Iterations"_) = int(values.at("Iterations"_)) +1;
                        if(values.contains("Iterations·Projection/Subsets"_)) values.remove("Iterations·Projection/Subsets"_);
                    }
                }
                if(float(values.at(bestName)) < best) {
                    best = values.at(bestName);
                    value = move(values.at(valueName));
                }
            }
            assert_(value.size, result);
            if(points.contains(configuration)) { log("Duplicate configuration", configuration); continue; }
            //assert_(!points.contains(configuration), name, configuration);
            points.insert(move(configuration), move(value));
        }
        if(smallestSubsets) points.filter([this](const Dict& configuration) {
            Dict config = copy(configuration);
            config.remove("per subset"_);
            uint min=-1; for(const Dict& c: points.keys) if(c.includes(config)) min = ::min(min, (uint)c.at("per subset"_));
            return (uint)configuration.at("per subset"_) > min; // Filters every configuration with larger subsets than minimum
        });
        if(limitAcquisitionTime) points.filter([this](const Dict& configuration) { return !configuration.at("Photons"_) || (uint)configuration.at("Projections"_) * (uint)configuration.at("Photons"_) > 256*4096; }); // Filters every configuration over acquisition time limit
        assert_(points, valueName);
        min = ::min(points.values);
        max = ::max(points.values);
        /*if(0) {
            array<Variant> values = copy(points.values);
            for(uint _unused i: range(2)) values.remove(::max(values)); // Removes 2 maximum values before computing maximum bound (discards outliers)
            max = ::max(values);
        } else { // Ignores SART values for maximum bound
            array<Variant> values = filterIndex(points.values, [this](size_t i){ return points.keys[i].at("Method"_)=="SART"_ || (this->valueName=="Time"_ && points.keys[i].at("Method"_)=="MLTR"_ && points.keys[i].at("per subset"_)==points.keys[i].at("Projections"_) ); });
            max = ::max(values);
        }*/
    }
    array<string> dimensions[2] = {split("Trajectory,Photons,Method"_,','),split("Revolutions,Projections,per subset"_,',')};

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
    int2 sizeHint() { return (levelCount().yx()+int2(1))*headerCellSize + cellCount() * contentCellSize; }

    uint renderHeader(int2 contentCellSize, uint axis, uint level, Dict& filter, uint offset=0) {
        if(level==dimensions[axis].size) return 1;
        string dimension = dimensions[axis][level];
        assert_(!filter.contains(dimension));
        uint cellCount = 0;
        auto coordinates = this->coordinates(dimension, filter);
        for(const Variant& coordinate: coordinates) {
            assert_(coordinate.size, dimension, filter, coordinates.size, coordinates);
            filter[String(dimension)] = copy(coordinate);
            uint childCellCount = renderHeader(contentCellSize, axis, level+1, filter, offset+cellCount);
            int2 headerOrigin (dimensions[!axis].size+1, level);
            int2 origin = int2(offset+cellCount, 0);
            int2 size = int2(childCellCount, 1);
            if(axis) headerOrigin=headerOrigin.yx(), origin=origin.yx(), size=size.yx();
            int2 cellSize = axis ? int2(headerCellSize.x, contentCellSize.y) : int2(contentCellSize.x, headerCellSize.y);
            origin = headerOrigin*headerCellSize + origin*cellSize;
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
                    Image cell = clip(target, (levelCount().yx()+int2(1))*headerCellSize+origin*cellSize+Rect(cellSize));
                    const Variant& point = points.at(coordinates);
                    assert_(isDecimal(point), point);
                    float value = point;
                    float v = max>min ? (value-min)/(max-min) : 0;
                    assert_(v>=0 && v<=1, v, value, min, max);
                    fill(cell, Rect(cell.size()), vec3(0,1-v,v));
                    float realValue = abs(value); // Values where maximum is best have been negated
                    String text = (value==min?format(Bold):""_)+(point.isInteger?dec(realValue):ftoa(realValue));
                    Text(text, textSize, black).render(cell);
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
        int2 cellSize = (target.size() - (levelCount().yx()+int2(1))*headerCellSize ) / cellCount();
        // Fixed coordinates in unused top-left corner
        String fixed;
        for(const auto& coordinate: coordinates(points)) if(coordinate.value.size==1) fixed << coordinate.key+": "_+str(coordinate.value)+"\n"_;
        Text(fixed, textSize).render(clip(target, Rect(int2(dimensions[1].size,dimensions[0].size)*headerCellSize)));
        // Value name in unused top-left cell
        Text(format(TextFormat::Bold)+valueName, textSize).render(clip(target, int2(dimensions[1].size,dimensions[0].size)*headerCellSize+Rect(headerCellSize)));
        // Dimensions
        for(uint axis: range(2)) for(uint level: range(dimensions[axis].size)) {
            int2 origin = int2(dimensions[!axis].size-1+1, level);
            if(axis) origin=origin.yx();
            string dimension = dimensions[axis][level];
            Text(dimension,textSize,black).render(clip(target, (origin*headerCellSize)+Rect(headerCellSize)));
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
    map<string, Variant> parameters = parseParameters(arguments()?arguments().slice(1):array<string>(),{"ui"_,"reference"_,"volumeSize"_,"projectionSize"_,"trajectory"_,"rotationCount"_,"photonCount"_,"projectionCount"_,"method"_,"subsetSize"_});
    ArrayView view { arguments()?arguments()[0]:"MSE_T"_, parameters};
    Window window {&view, "Results"_};
    FileWatcher watcher{"Results"_, [this](string){ view=ArrayView(view.valueName,parameters);/*Reloads*/ window.render(); } };
    Application() {
        for(string valueName: {"MSE_C"_,"MSE_E"_,"MSE_T"_,"Time (s)"_,"Iterations"_/*"SNR (dB)"_,"Normalized NMSE"_*/}) {
            ArrayView view (valueName, parameters, 64);
            Image image ( abs(view.sizeHint()) );
            assert_( image.size() < int2(16384), view.sizeHint(), view.levelCount(), view.cellCount());
            fill(image, Rect(image.size()), white);
            view.Widget::render( image );
            writeFile("Array "_+valueName, encodePNG(image));
        }
        window.setSize(-1);
        window.show();
    }
} app;
