#include "tiff.h"
#include "data.h"
#include "image.h"
#include <tiffio.h> //tiff

tsize_t tiffRead(BinaryData& s, byte* buffer, tsize_t size) { size=min(size,int(s.buffer.size-s.index)); copy(buffer, s.buffer.data+s.index, size); s.advance(size); return size; }
tsize_t tiffWrite(BinaryData&, byte*, tsize_t) { error(""); }
toff_t tiffSeek(BinaryData& s, toff_t off, int whence) {
    if(whence==SEEK_SET) s.index=off;
    if(whence==SEEK_CUR) s.index+=off;
    if(whence==SEEK_CUR) s.index=s.buffer.size+off;
    return s.index;
}
int tiffClose(BinaryData&) { return 0; }
toff_t tiffSize(BinaryData& fd) { return fd.buffer.size; }
int tiffMap(thandle_t , tdata_t* , toff_t* ) { return 0; }
void tiffUnmap(thandle_t, tdata_t , toff_t) {}

Image decodeTIFF(const ref<byte>& file) {
    BinaryData s (file);
    TIFF *const tiff = TIFFClientOpen("TIFF","r", (thandle_t)&s, (TIFFReadWriteProc)tiffRead, (TIFFReadWriteProc)tiffWrite, (TIFFSeekProc)tiffSeek, (TIFFCloseProc)tiffClose, (TIFFSizeProc)tiffSize, (TIFFMapFileProc)tiffMap, (TIFFUnmapFileProc)tiffUnmap);
    assert(tiff);
    uint32 width=0; TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    uint32 height=0; TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    Image image(width, height);
    TIFFReadRGBAImage(tiff, width, height, (uint32*)image.data); //FIXME: 16bit
    TIFFClose(tiff);
    return image;
}

Tiff16::Tiff16(const ref<byte>& file) : s(file) {
    TIFFSetWarningHandler(0);
    tiff = TIFFClientOpen("TIFF","r", (thandle_t)&s, (TIFFReadWriteProc)tiffRead, (TIFFReadWriteProc)tiffWrite, (TIFFSeekProc)tiffSeek, (TIFFCloseProc)tiffClose, (TIFFSizeProc)tiffSize, (TIFFMapFileProc)tiffMap, (TIFFUnmapFileProc)tiffUnmap);
    assert(tiff, file.size, hex(file.slice(0, 4)));
    if(!tiff) return;
    TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    uint rowsPerStrip=0; TIFFGetField(tiff, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip);
    uint compression=0; TIFFGetField(tiff, TIFFTAG_ROWSPERSTRIP, &compression);
    randomAccess = rowsPerStrip == 1 && compression == 1;
    uint16 bitPerSample=1; TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bitPerSample); assert(bitPerSample==16);
}
void Tiff16::read(uint16* target, uint x0, uint y0, uint w, uint h, uint stride) {
    assert(x0+w<=width && y0+h<=height, x0, y0, w, h, width, height);
    if(!randomAccess) for(uint y: range(y0)) { uint16 buffer[width]; TIFFReadScanline(tiff, buffer, y, 0); } // Reads first lines to avoid "Compression algorithm does not support random access" errors.
    if(w==width) for(uint y: range(h)) TIFFReadScanline(tiff, target+y*stride, y0+y, 0);
    else {
        for(uint y: range(h)) { uint16 buffer[width]; TIFFReadScanline(tiff, buffer, y0+y, 0); rawCopy(target+y*stride, buffer+x0, w); }
    }
}
Tiff16::~Tiff16() { TIFFClose(tiff); }
