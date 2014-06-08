#include "tiff.h"
#include "data.h"
#include "image.h"
#define int64 _int64
#define uint64 _uint64
#include <tiffio.h> //tiff
#undef int64
#undef uint64

static toff_t tiffSize(Data& fd) { return fd.buffer.size; }
static tsize_t tiffRead(Data& s, byte* buffer, tsize_t size) {
    size=min<tsize_t>(size,s.buffer.size-s.index);
    copy(mref<byte>(buffer, size), s.read(size));
    return size;
}
static toff_t tiffSeek(Data& s, toff_t off, int whence) {
    if(whence==SEEK_SET) s.index=off;
    if(whence==SEEK_CUR) s.index+=off;
    if(whence==SEEK_END) s.index=s.buffer.size+off;
    return s.index;
}
static tsize_t tiffZero() {return 0; }
static int tiffError() { error(""); return 0; }

Image decodeTIFF(const ref<byte>& file) {
    Data s (file);
    TIFF *const tiff = TIFFClientOpen("TIFF","r", (thandle_t)&s, (TIFFReadWriteProc)tiffRead, (TIFFReadWriteProc)tiffError, (TIFFSeekProc)tiffSeek, (TIFFCloseProc)tiffZero, (TIFFSizeProc)tiffSize, (TIFFMapFileProc)tiffZero, (TIFFUnmapFileProc)tiffError);
    assert_(tiff);
    uint32 width=0; TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    uint32 height=0; TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    Image image(width, height);
    TIFFReadRGBAImage(tiff, width, height, (uint32*)image.data); //FIXME: 16bit
    TIFFClose(tiff);
    return image;
}

Tiff16::Tiff16(const ref<byte>& file) : s(file) {
    TIFFSetWarningHandler(0);
    tiff = TIFFClientOpen("TIFF","r", (thandle_t)&s, (TIFFReadWriteProc)tiffRead, (TIFFReadWriteProc)tiffError, (TIFFSeekProc)tiffSeek, (TIFFCloseProc)tiffZero, (TIFFSizeProc)tiffSize, (TIFFMapFileProc)tiffZero, (TIFFUnmapFileProc)tiffError);
    assert_(tiff, file.size, hex(file.slice(0, 4)));
    if(!tiff) return;
    TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    uint rowsPerStrip=0; TIFFGetField(tiff, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip);
    randomAccess = rowsPerStrip == 1;
    uint16 bitPerSample=1; TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bitPerSample); assert_(bitPerSample==16);
}
void Tiff16::read(uint16* target, uint x0, uint y0, uint w, uint h, uint stride) {
    assert_(x0+w<=width && y0+h<=height, x0, y0, w, h, width, height);
    if(!randomAccess) for(uint y: range(y0)) { uint16 buffer[width]; assert_( TIFFReadScanline(tiff, buffer, y, 0) == 1 ); } // Reads first lines to avoid "Compression algorithm does not support random access" errors.
    if(w==width) for(uint y: range(h)) assert_( TIFFReadScanline(tiff, target+y*stride, y0+y, 0) == 1 );
    else {
        for(uint y: range(h)) {
            uint16 buffer[width];
            assert_( TIFFReadScanline(tiff, buffer, y0+y, 0)  == 1 );
            copy(mref<uint16>(target+y*stride,w), ref<uint16>(buffer+x0, w));
        }
    }
}
Tiff16::~Tiff16() { TIFFClose(tiff); }

static tsize_t tiffWrite(Data& s, const byte* data, tsize_t size) {
    assert_(size_t(s.index+size)<=s.buffer.capacity);
    copy(s.buffer.slice(s.index, size), ref<byte>(data, size));
    s.index+=size;
    if(s.index>s.buffer.size) s.buffer.size=s.index;
    return size;
}
buffer<byte> encodeTIFF(const Image16& image) {
    BinaryData s ( buffer<byte>(4096+image.height*image.width*2, 0) );
    TIFF* tiff = TIFFClientOpen("TIFF", "w", (thandle_t)&s, (TIFFReadWriteProc)tiffZero, (TIFFReadWriteProc)tiffWrite, (TIFFSeekProc)tiffSeek, (TIFFCloseProc)tiffZero, (TIFFSizeProc)tiffError, (TIFFMapFileProc)tiffError, (TIFFUnmapFileProc)tiffError);
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, image.width);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, image.height);
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    for(uint y: range(image.height)) TIFFWriteScanline(tiff, (void*)(image.data.begin()+y*image.width), y, 0);
    TIFFClose(tiff);
    return move(s.buffer);
}