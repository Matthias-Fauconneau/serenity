#include "tiff.h"
#include "data.h"
#include "image.h"
#include <tiffio.h> //tiff

static toff_t tiffSize(BinaryData& fd) { return fd.buffer.size; }
static tsize_t tiffRead(BinaryData& s, byte* buffer, tsize_t size) { size=min(size,int(s.buffer.size-s.index)); copy(buffer, s.buffer.data+s.index, size); s.advance(size); return size; }
static toff_t tiffSeek(BinaryData& s, toff_t off, int whence) {
    if(whence==SEEK_SET) s.index=off;
    if(whence==SEEK_CUR) s.index+=off;
    if(whence==SEEK_END) s.index=s.buffer.size+off;
    return s.index;
}
static tsize_t tiffZero() {return 0; }
static int tiffError() { error(""); return 0; }

Image decodeTIFF(const string& file) {
    BinaryData s (file);
    TIFF *const tiff = TIFFClientOpen("TIFF","r", (thandle_t)&s, (TIFFReadWriteProc)tiffRead, (TIFFReadWriteProc)tiffError, (TIFFSeekProc)tiffSeek, (TIFFCloseProc)tiffZero, (TIFFSizeProc)tiffSize, (TIFFMapFileProc)tiffZero, (TIFFUnmapFileProc)tiffError);
    assert(tiff);
    uint32 width=0; TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    uint32 height=0; TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    Image image(width, height);
    TIFFReadRGBAImage(tiff, width, height, (uint32*)image.data); //FIXME: 16bit
    TIFFClose(tiff);
    return image;
}

Tiff16::Tiff16(const string& file) : s(file) {
    TIFFSetWarningHandler(0);
    tiff = TIFFClientOpen("TIFF","r", (thandle_t)&s, (TIFFReadWriteProc)tiffRead, (TIFFReadWriteProc)tiffError, (TIFFSeekProc)tiffSeek, (TIFFCloseProc)tiffZero, (TIFFSizeProc)tiffSize, (TIFFMapFileProc)tiffZero, (TIFFUnmapFileProc)tiffError);
    assert(tiff);
    if(!tiff) return;
    TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    uint rowsPerStrip=0; TIFFGetField(tiff, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip);
    randomAccess = rowsPerStrip == 1;
    uint16 bitPerSample=1; TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bitPerSample); assert(bitPerSample==16);
}
void Tiff16::read(uint16* target, uint x0, uint y0, uint w, uint h, uint stride) {
    assert(x0+w<=width && y0+h<=height);
    if(!randomAccess) for(uint y: range(y0)) { uint16 buffer[width]; assert( TIFFReadScanline(tiff, buffer, y, 0) == 1 ); } // Reads first lines to avoid "Compression algorithm does not support random access" errors.
    if(w==width) for(uint y: range(h)) assert( TIFFReadScanline(tiff, target+y*stride, y0+y, 0) == 1 );
    else {
        for(uint y: range(h)) { uint16 buffer[width]; assert( TIFFReadScanline(tiff, buffer, y0+y, 0)  == 1 ); rawCopy(target+y*stride, buffer+x0, w); }
    }
}
Tiff16::~Tiff16() { TIFFClose(tiff); }

static tsize_t tiffWrite(BinaryData& s, const byte* data, tsize_t size) { assert(s.index+size<=s.buffer.capacity); copy(s.buffer.begin()+s.index, data, size); s.index+=size; if(s.index>s.buffer.size) s.buffer.size=s.index; return size; }
buffer<byte> encodeTIFF(const Image16& image) {
    BinaryData s ( buffer<byte>(4096+image.height*image.width*2, 0) );
    TIFF* tiff = TIFFClientOpen("TIFF", "w", (thandle_t)&s, (TIFFReadWriteProc)tiffZero, (TIFFReadWriteProc)tiffWrite, (TIFFSeekProc)tiffSeek, (TIFFCloseProc)tiffZero, (TIFFSizeProc)tiffError, (TIFFMapFileProc)tiffError, (TIFFUnmapFileProc)tiffError);
    TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, image.width);
    TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, image.height);
    TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 1);
    TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16);
    TIFFSetField(tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    for(uint y: range(image.height)) TIFFWriteScanline(tiff, (void*)(image.data+y*image.width), y, 0);
    TIFFClose(tiff);
    return move(s.buffer);
}
