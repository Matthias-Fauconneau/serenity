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
	assert_(s.index  <= s.data.size);
	size = min<tsize_t>(size, s.data.size - s.index);
	log(s.data.size, s.index, size);
	mref<byte>(buffer, size).copy(s.read(size));
    return size;
}
static toff_t tiffSeek(Data& s, toff_t off, int whence) {
    if(whence==SEEK_SET) s.index=off;
    if(whence==SEEK_CUR) s.index+=off;
	if(whence==SEEK_END) s.index=s.data.size+off;
    return s.index;
}
static tsize_t tiffZero() { return 0; }
static int tiffError() { error(""); return 0; }

Tiff16::Tiff16(const ref<byte> file) : s(file) {
    tiff = TIFFClientOpen("TIFF","r", (thandle_t)&s, (TIFFReadWriteProc)tiffRead, (TIFFReadWriteProc)tiffError, (TIFFSeekProc)tiffSeek, (TIFFCloseProc)tiffZero, (TIFFSizeProc)tiffSize, (TIFFMapFileProc)tiffZero, (TIFFUnmapFileProc)tiffError);
    assert_(tiff, file.size, hex(file.slice(0, 4)));
    if(!tiff) return;
    TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    uint rowsPerStrip=0; TIFFGetField(tiff, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip);
    randomAccess = rowsPerStrip == 1;
    uint16 bitPerSample=1; TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bitPerSample); assert_(bitPerSample==16);
	assert_(TIFFScanlineSize(tiff) == width*2);
}
void Tiff16::read(uint16* target, uint x0, uint y0, uint w, uint h, uint stride) {
    assert_(x0+w<=width && y0+h<=height, x0, y0, w, h, width, height);
    if(!randomAccess) for(uint y: range(y0)) { uint16 buffer[width]; assert_( TIFFReadScanline(tiff, buffer, y, 0) == 1 ); } // Reads first lines to avoid "Compression algorithm does not support random access" errors.
    if(w==width) for(uint y: range(h)) assert_( TIFFReadScanline(tiff, target+y*stride, y0+y, 0) == 1 );
    else {
        for(uint y: range(h)) {
			uint16 buffer [width];
			assert_( TIFFReadScanline(tiff, buffer, y0+y, 0)  == 1 );
			mref<uint16>(target+y*stride,w).copy(ref<uint16>(buffer+x0, w));
        }
    }
}
Tiff16::~Tiff16() { TIFFClose(tiff); }
