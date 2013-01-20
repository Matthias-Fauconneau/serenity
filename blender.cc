#include "blender.h"
#include "process.h"
#include "string.h"
#include "data.h"
#include "gl.h"

/// Used to fix pointers in .blend file-block sections
struct Block {
    uint64 begin; // First memory address used by pointers pointing in this block
    uint64 end; // Last memory address used by pointers pointing in this block
    int64 delta; // Target memory address where the block was loaded - begin
};
bool operator<(const Block& a, const Block& b) { return a.begin<b.begin; }

/// SDNA type definitions
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

/// Parses a .blend file
struct Blender {
    Map map; // Keep file mmaped
    const Scene* scene=0; // Blender scene (root handle to access all data)

    struct Vertex {
        vec3 position; // World-space position
        vec3 color; // BGR albedo (TODO: texture mapping)
        vec3 normal; // World-space vertex normals
    };

    array<GLBuffer> buffers;

    Blender()
        : map("Island/Island.blend", home(), Map::Read|Map::Write) { // with write access to fix pointers (TODO: write back fixes for faster loads)
        load();
        parse();
    }

    /// Recursively fix all pointers in an SDNA structure
    void fix(const array<Block>& blocks, const Struct& type, const ref<byte>& buffer) {
        BinaryData data (buffer);
        for(const Struct::Field& field: type.fields) {
            if(!field.reference) {
                if(field.type->fields) for(uint i unused: range(field.count)) fix(blocks, *field.type, data.Data::read(field.type->size));
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

    /// Parses SDNA to fix all pointers
    void load() {
        array<Block> blocks; // List of file blocks containing pointers to be fixed
        array<Struct> structs; // SDNA structure definitions

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
                        for(const Struct& match: structs) if(match.name == f.typeName) { f.type = &match; break; }
                        assert(f.type || f.reference);
                    }
                }
            }
            if(identifier == "ENDB"_) break;
        }
        quicksort(blocks);

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
                if(type.fields) for(uint unused i: range(header.count)) fix(blocks, type, data.read(type.size));
            }
        }

        //for(const Struct& match: structs) if(match.name == "MLoop"_) { log(match); break; }
    }

    /// Extracts relevant data from Blender structures
    void parse() {
        for(const Base& base: scene->base) {
            if(base.object->type==Object::Mesh) {
                const Mesh* mesh = base.object->data;

                ref<MVert> verts (mesh->mvert, mesh->totvert);
                array<Vertex> vertices;
                for(const MVert& vert: verts) {
                    vertices << Vertex __(vec3(vert.co), normalize(vec3(vert.no[0],vert.no[1],vert.no[2])), vec3(1,1,1)); //TODO: MCol
                }

                ref<MPoly> polys (mesh->mpoly, mesh->totpoly);
                ref<MLoop> loops (mesh->mloop, mesh->totloop);
                array<uint> indices;
                for(const MPoly& poly: polys) {
                    assert(poly.totloop==3 || poly.totloop==4);
                    uint a=loops[poly.loopstart].v, b=loops[poly.loopstart+1].v;
                    for(uint i: range(poly.loopstart+2, poly.loopstart+poly.totloop)) {
                        uint c = loops[i].v;
                        indices << a << b << c;
                        b = c;
                    }
                }

                /*// Submits geometry
                GLBuffer buffer;
                buffer.upload<Vertex>(vertices);
                buffer.upload(indices);
                buffers << move(buffer);*/

                log(base.object->id.name, mesh->id.name, vertices.size(), indices.size());
            }
        }
    }
} application;
