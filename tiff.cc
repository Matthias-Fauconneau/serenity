#include "data.h"
#include "image.h"
#include <tiffio.h>

tsize_t tiffRead(BinaryData& s, uint8* buffer, tsize_t size) { s.read<uint8>(buffer, size); return size; }
tsize_t tiffWrite(BinaryData&, uint8*, tsize_t) { error(""); }
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
    TIFF *const tiff = TIFFClientOpen("foo","r", (thandle_t)&s, (TIFFReadWriteProc)tiffRead, (TIFFReadWriteProc)tiffWrite, (TIFFSeekProc)tiffSeek, (TIFFCloseProc)tiffClose, (TIFFSizeProc)tiffSize, (TIFFMapFileProc)tiffMap, (TIFFUnmapFileProc)tiffUnmap);
    assert(tiff);
    uint32 width=0; TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width);
    uint32 height=0; TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height);
    uint16 photometric=0; TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric);
    uint16 bitPerSample=1; TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bitPerSample);
    uint16 samplesPerPixel=1; TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
    Image image(width, height);
    TIFFReadRGBAImage(tiff, width, height, (uint32*)image.data); //FIXME: 16bit
    TIFFClose(tiff);
    return image;
}
