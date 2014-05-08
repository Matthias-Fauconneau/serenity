#include "thread.h"
#include "netcdf.h"
#include "volume.h"
#include "view.h"
#include "project.h"
#include "layout.h"
#include "window.h"

struct Volume {
    Map map;
    VolumeF volume;

    Volume(const Folder& folder) {
        array<String> files = folder.list();
        for(string name: files) {
            TextData s (name);
            int X = s.mayInteger(); if(!s.match("x"_)) continue;
            int Y = s.mayInteger(); if(!s.match("x"_)) continue;
            int Z = s.mayInteger();
            File file (name, folder);
            if(file.size() != X*Y*Z*sizeof(float)) { log("Wrong size"); continue; }
            int64 lastModified=0; for(string file: files) lastModified = max(lastModified, File(file, folder).modifiedTime());
            if(lastModified > file.modifiedTime()) continue;
            map = file;
            volume = VolumeF(int3(X,Y,Z), cast<float>((ref<byte>)map));
        }
        if(!volume) { // Generates memory map-able (little endian) data
            uint X = 0, Y = 0, Z = 0;
            for(string name: files) {
                if(!startsWith(name,"block"_) || !endsWith(name,".nc"_)) continue;
                Map file = File(name, folder);
                NetCDF cdf ( file );
                ref<uint> dimensions = cdf.variables.values[1].dimensions.values;
                if(X) assert_( X == dimensions[2] ); else X = dimensions[2];
                if(Y) assert_( Y == dimensions[1] ); else Y = dimensions[1];
                Z += dimensions[0];
            }
            File file (dec(X)+"x"_+dec(Y)+"x"_+dec(Z), folder, Flags(Create|ReadWrite));
            file.resize(X*Y*Z*sizeof(float));
            map = Map(file, Map::Prot(Map::Read|Map::Write));
            mref<float> data ((float*)map.data.pointer,map.size/sizeof(float));
            uint z = 0;
            for(string name: files) {
                if(!startsWith(name,"block"_) || !endsWith(name,".nc"_)) continue;
                log(name);
                Map file = File(name, folder);
                NetCDF cdf ( file );
                ref<uint> dimensions = cdf.variables.values[1].dimensions.values;
                int Z = dimensions[0];
                ref<int32> source = cast<int32>( ref<float>(cdf.variables.values[1]) );
                mref<int32> target ((int32*)data.data+z*Y*X, Z*Y*X);
                for(uint i: range(Z*Y*X)) target[i] = bswap(source[i]);
                z += dimensions[0];
            }
            volume = VolumeF(int3(X,Y,Z), data);
        }
    }
};

struct Viewer {
    array<unique<Volume>> volumes;
    HList<View> views;
    Window window { &views, "Viewer"_};
    Viewer() {
        Folder folder {"Results"_, home()};
        array<String> names = folder.list(Folders);
        for(String& name: names) {
            volumes << unique<Volume>(Folder(name, folder));
            views << View( &volumes.last()->volume );
        }
        window.setSize(int2(views.size*views[0].sizeHint().x, views[0].sizeHint().y));
        window.setTitle(str(names));
        window.show();
    }
} app;
