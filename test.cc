#if 0
#include "process.h"
struct LogTest {
    LogTest(){ log("Hello World"_); }
} test;
#endif

#if 0
#include "asound.h"
const float PI = 3.14159265358979323846;
inline float cos(float t) { return __builtin_cosf(t); }
inline float sin(float t) { return __builtin_sinf(t); }
struct SoundTest {
    AudioOutput audio __({this, &SoundTest::read}, 48000, 4096);
    SoundTest() { audio.start(); }
    float step=2*PI*440/48000;
    float amplitude=0x1p12;
    float phase=0;
    bool read(int16* output, uint periodSize) {
        for(uint i : range(periodSize)) {
            float sample = amplitude*sin(phase);
            output[2*i+0] = sample, output[2*i+1] = sample;
            phase += step;
        }
        return true;
    }
} test;
#endif

#if 0
#include "window.h"
#include "html.h"
struct HTMLTest {
    Scroll<HTML> page;
    Window window __(&page.area(),0,"HTML"_);

    HTMLTest() {
        window.localShortcut(Escape).connect(&exit);
        page.contentChanged.connect(&window, &Window::render);
        page.go(""_);
    }
} test;
#endif

#if 1
#include "process.h"
#include "string.h"
#include "data.h"
#include "blender.h"

// Quicksort
template<class T> uint partition(array<T>& at, uint left, uint right, uint pivotIndex) {
    swap(at[pivotIndex], at[right]);
    const T& pivot = at[right];
    uint storeIndex = left;
    for(uint i: range(left,right)) {
        if(at[i] < pivot) {
            swap(at[i], at[storeIndex]);
            storeIndex++;
        }
    }
    swap(at[storeIndex], at[right]);
    return storeIndex;
}
template<class T> void quicksort(array<T>& at, uint left, uint right) {
    if(left < right) { // If the list has 2 or more items
        uint pivotIndex = partition(at, left, right, (left + right)/2);
        quicksort(at, left, pivotIndex-1);
        quicksort(at, pivotIndex+1, right);
    }
}
template<class T> void quicksort(array<T>& at) { return quicksort(at, 0, at.size()-1); }

struct BlockHeader {
    char identifier[4];
    uint size; // Total length of the data after the block header
    uint64 address; // Base memory address used by pointers pointing in this block
    uint type; // Type of the stored structure (as an index in SDNA types)
    uint count; // Number of structures located in this block
};

struct Block {
    uint64 begin; // First memory address used by pointers pointing in this block
    uint64 end; // Last memory address used by pointers pointing in this block
    int64 delta; // Target memory address where the block was loaded - begin
};
bool operator<(const Block& a, const Block& b) { return a.begin<b.begin; }

struct Struct {
    ref<byte> name;
    uint size;
    struct Field {
        ref<byte> typeName;
        uint reference;
        ref<byte> name;
        uint count;
        const Struct* type;
    };
    array<Field> fields;
};
string str(const Struct::Field& f) { return " "_+f.typeName+(f.reference?"* "_:" "_)+f.name+(f.count!=1?"["_+str(f.count)+"]"_:string())+";"_; }
string str(const Struct& s) { return "struct "_+s.name+" {\n"_+str(s.fields,'\n')+"\n};"_; }

struct ReadBlend {
    array<Block> blocks; // List of file blocks containing pointers to be fixed
    array<Struct> structs; // SDNA structure definitions

    const Struct* type(const ref<byte>& name) { for(const Struct& match: structs) if(name == match.name) return &match; return 0; }

    // Recursively fix all pointers in a structure
    void fix(const Struct& type, const ref<byte>& buffer) {
        BinaryData data (buffer);
        for(const Struct::Field& field: type.fields) {
            if(!field.reference) {
                if(field.type->fields) for(uint i unused: range(field.count)) fix(*field.type, data.Data::read(field.type->size));
                else data.advance(field.count*field.type->size);
            } else for(uint i unused: range(field.count)) {
                uint64& pointer = (uint64&)data.read<uint64>();
                if(!pointer) continue;
                for(const Block& block: blocks.slice(blocks.binarySearch( Block __(pointer) )-1)) {
                    if(pointer >= block.begin && pointer < block.end) {
                        pointer += block.delta;
                        goto found;
                    }
                }
                pointer = 0;
                found:;
            }
        }
    }

    ReadBlend() {
        Map map("Island/Island.blend", home(), Map::Read|Map::Write); // with write access to fix pointers
        BinaryData file(map);
        //Assumes BLENDER-v262 (64bit little endian)
        file.seek(12);
        blocks.reserve(32768);
        while(file) { // Parses SDNA
            const BlockHeader& header = file.read<BlockHeader>();
            ref<byte> identifier(header.identifier,4);
            BinaryData data( file.Data::read(header.size) );
            blocks << Block __(header.address, header.address+header.size, int64(uint64(data.buffer.buffer.data)-header.address));
            if(identifier == "DNA1"_) {
                data.advance(4); //SDNA
                data.advance(4); //NAME
                uint nameCount = data.read();
                array< ref<byte> > names;
                for(uint unused i: range(nameCount)) names << data.untilNull();
                data.align(4);
                data.advance(4); //TYPE
                uint typeCount = data.read();
                array< ref<byte> > types;
                for(uint unused i: range(typeCount)) types << data.untilNull();
                data.align(4);
                data.advance(4); //TLEN
                ref<uint16> lengths = data.read<uint16>(typeCount);
                data.align(4);
                data.advance(4); //STRC
                uint structCount = data.read();
                for(uint unused i: range(structCount)) {
                    Struct s;
                    uint16 name = data.read();
                    s.size = lengths[name];
                    s.name = types[name];
                    uint16 fieldCount = data.read();
                    for(uint unused i: range(fieldCount)) {
                        uint16 type = data.read();
                        uint16 name = data.read();
                        Struct::Field f __(types[type], 0, names[name], 1, 0);
                        if(f.name[0]=='(') { //parse function pointers
                            f.name.data+=2; f.name.size-=2+3;
                            f.reference++;
                        } else {
                            while(f.name[0]=='*') { //parse references
                                f.name.data++; f.name.size--;
                                f.reference++;
                            }
                        }
                        for(uint i=0;i<f.name.size;i++) { //parse static arrays
                            if(f.name[i]=='[') {
                                f.count = toInteger(f.name.slice(i+1,f.name.size-1-(i+1)));
                                f.name.size = i;
                            }
                        }
                        s.fields << f;
                    }
                    structs << move(s);
                }
                structs << Struct __("char"_,1) << Struct __("short"_,2) << Struct __("int"_,4) << Struct __("uint64_t"_,8)
                        << Struct __("float"_,4) << Struct __("double"_,8);
                for(Struct& s: structs) {
                    for(Struct::Field& f: s.fields) {
                        f.type=type(f.typeName);
                        if(!f.type && !f.reference) error("Undefined",f);
                    }
                }
            }
            if(identifier == "ENDB"_) break;
        }
        quicksort(blocks);
        //for(Block& a: blocks) for(Block& b: blocks) if(a < b) assert(&a < &b); else assert(&a >= &b);

        const Scene* scene=0;
        file.seek(12);
        while(file) { // Fixes pointers
            const BlockHeader& header = file.read<BlockHeader>();
            ref<byte> identifier(header.identifier,4);
            Data data (file.Data::read(header.size));

            if(identifier == "DNA1"_) continue;
            if(identifier == "ENDB"_) break;
            if(identifier == "SC\0\0"_) scene = (Scene*)data.buffer.buffer.data;

            const Struct& type = structs[header.type];
            if(header.size < header.count*type.size) {
                //log("WARNING: header.size < header.count*type.size", identifier, header.count, type.size, header.size, type);
            } else {
                if(type.fields) for(uint unused i: range(header.count)) fix(type, data.read(type.size));
            }
        }
        for(const Base& base: scene->base) {
            log(base.object->id.name);
        }
        //log(scene->base->first->id.name);
    }
} test;
#endif
