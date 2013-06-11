#include "thread.h"
#include "sample.h"
#include "data.h"

/// Concatenate sweep results into a single file //FIXME: integrate in process manager
struct Concatenate {
    Summary(const ref<ref<byte>>& arguments) {
        Folder folder = arguments[0];
        Sample volume;
        for(const ref<byte>& file: folder.list(Files)) {
            TextData s(file); s.whileNo("0123456789"_); uint minimalDiameter=s.integer(); s.skip(".tsv"_); assert(!s);
            if(minimalDiameter>=volume.size) volume.grow(minimalDiameter+1);
            volume[minimalDiameter] = sum(parseUniformSample(readFile(file,folder)));
        }
        writeFile("volume.tsv",toASCII(volume),folder);
    }
} script ( arguments() );
