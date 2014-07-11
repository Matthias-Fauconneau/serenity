#include "thread.h"
#include "variant.h"
#include "text.h"
#include "window.h"
#include "graphics.h"
#include "png.h"
#include "synthetic.h"
#include "deflate.h"

PorousRock rock (256);
VolumeF reference = rock.volume();

struct SliceArrayView : Widget {
    Dict dimensionLabels = parseDict("rotationCount:Rotations,photonCount:Photons,projectionCount:Projections,subsetSize:per subset"_);
    Dict coordinateLabels = parseDict("single:\0Single,double:\1Double,adaptive:\2Adaptive"_); // Sorts
    map<Dict, Image> points; // Data points
    uint sliceIndex;
    uint textSize;
    int2 textCellSize = int2(80*textSize/16, textSize); //TODO: merge ArrayView and SliceArrayView (and backport textCellSize)

    SliceArrayView(string valueName, uint sliceIndex, const map<string, Variant>& parameters, uint textSize=16) : sliceIndex(sliceIndex), textSize(textSize) {
        ImageF reference = slice(::reference, sliceIndex);
        ImageF image ( reference.size );
        Folder results = "Results"_;
        for(string name: results.list()) {
            if(!endsWith(name, ".best"_)) continue;
            Dict configuration = parseDict(section(name,'.',0,-2));
            if(configuration["trajectory"_]=="adaptive"_ && (int(configuration.at("rotationCount"_))==1||int(configuration.at("rotationCount"_))==4)) continue; // Skips 1,4 adaptive rotations (only 2,3,5 is relevant)
            if(configuration["trajectory"_]=="adaptive"_) configuration.at("rotationCount"_) = int(configuration["rotationCount"_])-1; // Converts adaptive total rotation count to helical rotation count
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

            Map map (name, results);
            buffer<float> data (map);
            int3 size = ::reference.size;
            if(data.size != (size_t)size.x*size.y*size.z) {
                static ::map<String, buffer<float>> cache;
                if(!cache.contains(name)) { log_(str("Inflate",name,"..."_)); Time time; cache.insert(copy(String(name)), cast<float>(inflate(map))); log(time); }
                data = buffer<float>(ref<float>(cache.at(name)));
            }
            VolumeF reconstruction (size, move(data), "x"_);
            Image target ( image.size );
            if(valueName=="x"_) {
                image =  slice(reconstruction, sliceIndex);
                convert(target, image, rock.containerAttenuation);
            } else if(valueName=="error"_) {
                const float* x0 = reference.data;
                const float* x1 = slice(reconstruction, sliceIndex).data;
                float* e = image.data;
                for(uint i: range(size.y*size.x)) e[i] = abs(x1[i] - x0[i]);
                convert(target, image, rock.containerAttenuation);
            } else error(valueName);
            Folder slice("Slices"_, currentWorkingDirectory(), true);
            writeFile(str(name,valueName,sliceIndex), encodePNG(target), slice);
            points.insert(move(configuration), move(target));
        }
        assert_(points);
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
    /// Returns coordinates occuring in \a pointCoordinates
    map<String, array<Variant>> coordinates(const array<Dict>& pointCoordinates) const {
        map<String, array<Variant>> allCoordinates;
        for(const Dict& coordinates: pointCoordinates) for(const auto coordinate: coordinates)
            if(!allCoordinates[copy(coordinate.key)].contains(coordinate.value)) allCoordinates.at(coordinate.key).insertSorted(copy(coordinate.value));
        return allCoordinates;
    }
    /// Returns number of cells for the given \a axis, \a level and \a coordinates
    int cellCount(uint axis, uint level, Dict& filter) const {
        if(level == dimensions[axis].size) return 1;
        string dimension = dimensions[axis][level];
        int cellCount = 0;
        assert_(!filter.contains(dimension));
        assert_(coordinates(dimension, filter), dimension, filter);
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
    int2 sizeHint() { return (levelCount().yx()+int2(1))*textCellSize + cellCount() * int2(256*sqrt(2.)/2); }

    uint renderHeader(int2 cellSize, uint axis, uint level, Dict& filter, uint offset=0) {
        if(level==dimensions[axis].size) return 1;
        string dimension = dimensions[axis][level];
        assert_(!filter.contains(dimension));
        uint cellCount = 0;
        for(const Variant& coordinate: coordinates(dimension, filter)) {
            filter[String(dimension)] = copy(coordinate);
            uint childCellCount = renderHeader(cellSize, axis, level+1, filter, offset+cellCount);
            int2 headerOrigin (dimensions[!axis].size+1, level);
            int2 origin = int2(offset+cellCount, 0);
            int2 size = int2(childCellCount, 1);
            if(axis) headerOrigin=headerOrigin.yx(), origin=origin.yx(), size=size.yx();
            int2 headerCellSize = axis ? int2(textCellSize.x, cellSize.y) : int2(cellSize.x, textCellSize.y);
            origin = headerOrigin*textCellSize + origin*headerCellSize;
            if(level<dimensions[axis].size-1) {
                int width = dimensions[axis].size-1-level;
                if(!axis) fill(target, Rect(origin+int2(-width/2,0),int2(origin.x+(width+1)/2, target.size().y)));
                if(axis) fill(target, Rect(origin+int2(0,-width/2),int2(target.size().x,origin.y+(width+1)/2)));
            }
            String label = copy(coordinate);
            if(label[0] < 16) label.removeAt(0); // Removes sort key
            Text(label, textSize, black).render(clip(target, origin+Rect(size*headerCellSize)));
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
                    Image cell = clip(target, (levelCount().yx()+int2(1))*textCellSize+origin*cellSize+Rect(cellSize));
                    const Image& slice = points.at(coordinates);
                    blit(cell, (cell.size()-slice.size())/2, slice);
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
        int2 cellSize = (target.size() - (levelCount().yx()+int2(1))*textCellSize ) / cellCount();
        // Fixed coordinates in unused top-left corner
        String fixed;
        for(const auto& coordinate: coordinates(points.keys)) if(coordinate.value.size==1) fixed << coordinate.key+": "_+str(coordinate.value)+"\n"_;
        Text(fixed, textSize).render(clip(target, Rect(int2(dimensions[1].size,dimensions[0].size)*textCellSize)));
        // Value name in unused top-left cell
        Text(format(TextFormat::Bold)+str(sliceIndex), textSize).render(clip(target, int2(dimensions[1].size,dimensions[0].size)*textCellSize+Rect(textCellSize)));
        // Dimensions
        for(uint axis: range(2)) for(uint level: range(dimensions[axis].size)) {
            int2 origin = int2(dimensions[!axis].size-1+1, level);
            if(axis) origin=origin.yx();
            string dimension = dimensions[axis][level];
            Text(dimension,textSize,black).render(clip(target, origin*textCellSize+Rect(textCellSize)));
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

#if 0
struct Application {
    SliceArrayView view {"x"_,256/2};
    unique<Window> window = /*arguments().contains("ui"_)*/true ? unique<Window>(&view, "Results"_) : nullptr;
    FileWatcher watcher{"Results"_, [this](string){ view=SliceArrayView("x"_,view.sliceIndex); if(window) window->render(); } };
    Application() {
        if(window) {
            window->setSize(-1);
            window->show();
        }
    }
} app;
#else
struct Application {
    map<string, Variant> parameters = parseParameters(arguments(),{"ui"_,"reference"_,"volumeSize"_,"projectionSize"_,"trajectory"_,"rotationCount"_,"photonCount"_,"projectionCount"_,"method"_,"subsetSize"_});
    Application() {
        Folder slice("Slices"_, currentWorkingDirectory(), true);
        for(string valueName: {"x"_,"error"_}) {
            for(uint z: {256/2,256/8}) {
                log(z);
                SliceArrayView view (valueName, z, parameters, 32);
                Image image ( abs(view.sizeHint()) );
                assert_( image.size() < int2(16384), view.sizeHint(), view.levelCount(), view.cellCount());
                fill(image, Rect(image.size()), white);
                view.Widget::render( image );
                writeFile(str(valueName, z), encodePNG(image),slice);
            }
        }
        log("Done");
    }
} app;
#endif
