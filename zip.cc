#include "zip.h"
#include "inflate.h"

struct LocalHeader {
    ubyte signature[4] = {'P','K', 3, 4};  //local file header signature
    uint16 features; //version needed to extract
    uint16 flag; //general purpose bit flag
    uint16 compression; //compression method
    uint16 modifiedTime; //last mod file time
    uint16 modifiedDate; //last mod file date
    uint crc; //crc-32
    uint compressedSize; //compressed size
    uint size; //uncompressed size
    uint16 nameLength; //file name length
    uint16 extraLength; //extra field length
    //file name (variable size)
    //extra field (variable size)
} packed;

struct DataDescriptor {
    uint crc; //crc-32
    uint compressedSize; //compressed size
    uint size; //uncompressed size
} packed;

struct FileHeader {
    ubyte signature[4] = {'P','K', 1, 2}; //central file header signature
    uint16 zipVersion; //version made by
    uint16 features; //version needed to extract
    uint16 flag; //general purpose bit flag
    uint16 compression; //compression method
    uint16 modifiedTime; //last mod file time
    uint16 modifiedDate; //last mod file date
    uint crc; //crc-32
    uint compressedSize; //compressed size
    uint size; //uncompressed size
    uint16 nameLength; //file name length
    uint16 extraLength; //extra field length
    uint16 commentLength; //file comment length
    uint16 disk; //disk number start
    uint16 attributes; //internal file attributes
    uint externalAttributes; //external file attributes
    uint offset; //relative offset of local header
    //file name (variable size)
    //extra field (variable size)
    //file comment (variable size)
} packed;

struct DirectoryEnd {
    ubyte signature[4] = {'P','K', 5, 6};
    uint16 disk;
    uint16 startDisk;
    uint16 nofEntries;
    uint16 nofTotalEntries;
    uint size;
    uint offset;
    uint16 commentLength;
} packed;

map< string,array<byte> > readZip(DataBuffer s) {
    map< string,array<byte> > files;
    s.until(DirectoryEnd().signature);
    DirectoryEnd directory = s.read();
    for(int i=0;i<directory.nofEntries;i++) {
        s.seek(directory.offset);
        FileHeader header = s.read();
        string name = s.read(header.nameLength);
        s.advance(header.extraLength);
        s.advance(header.commentLength);
        directory.offset = s.index;
        if(!endsWith(name,"/"_)) {
            s.seek(header.offset);
            LocalHeader local = s.read();
            s.advance(local.nameLength);
            s.advance(local.extraLength);
            auto data = s.read(header.compressedSize);
            if(header.compression==8) data=inflate(data, false);
            else assert(header.compression==0);
            files.insert(move(name), move(data));
        }
    }
    return files;
}

/*class File {
    string name;
    FileHeader header;
    LocalHeader local;
    ubyte[] raw;
    ubyte[] _data;
    this( string name, FileHeader header, LocalHeader local, ubyte[] raw ) {
 this.name=name; this.header=header; this.local=local; this.raw=raw; }
    string str() {
        return .str(header.size)~"\t"~
            (header.compression==0?"Raw":header.compression==8?"Deflate":"Unknow")~"\t"~
            .str(header.compressedSize)~"\t"~.str(header.features)~"\t"~name;
    }
    ubyte[] data() {
        if( !_data && header.size ) {
            if( header.compression == 0 ) _data = raw;
            else if( header.compression == 8 ) _data=inflate( new BinaryStream(raw) );
            if( !_data ) error(str);
        }
        return _data;
    }
    void data( ubyte[] data ) { _data=data;
        header.compression = 0; //if( header.size == 0 ) { header.compression = 0; }
        if( header.compression == 0 ) raw=data;
        else if( header.compression == 8 ) raw=deflate( data );
        else error();
        local.compression = header.compression;
        local.size = header.size = data.length;
        local.compressedSize = header.compressedSize = raw.length;
        local.crc = header.crc = CRC( data );
    }
}*/
