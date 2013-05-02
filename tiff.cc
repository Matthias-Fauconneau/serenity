extern "C" {
#include <tiffio.h>
}

tsize_t qtiffReadProc(thandle_t fd, tdata_t buf, tsize_t size)
{
    QIODevice* device = static_cast<QTiffHandler*>(fd)->device();
    return device->isReadable() ? device->read(static_cast<char *>(buf), size) : -1;
}

tsize_t qtiffWriteProc(thandle_t fd, tdata_t buf, tsize_t size)
{
    return static_cast<QTiffHandler*>(fd)->device()->write(static_cast<char *>(buf), size);
}

toff_t qtiffSeekProc(thandle_t fd, toff_t off, int whence)
{
    QIODevice *device = static_cast<QTiffHandler*>(fd)->device();
    switch (whence) {
    case SEEK_SET:
        device->seek(off);
        break;
    case SEEK_CUR:
        device->seek(device->pos() + off);
        break;
    case SEEK_END:
        device->seek(device->size() + off);
        break;
    }

    return device->pos();
}

int qtiffCloseProc(thandle_t /*fd*/)
{
    return 0;
}

toff_t qtiffSizeProc(thandle_t fd)
{
    return static_cast<QTiffHandler*>(fd)->device()->size();
}

int qtiffMapProc(thandle_t /*fd*/, tdata_t* /*pbase*/, toff_t* /*psize*/)
{
    return 0;
}

void qtiffUnmapProc(thandle_t /*fd*/, tdata_t /*base*/, toff_t /*size*/)
{
}

// for 32 bits images
inline void rotate_right_mirror_horizontal(QImage *const image)// rotate right->mirrored horizontal
{
    const int height = image->height();
    const int width = image->width();
    QImage generated(/* width = */ height, /* height = */ width, image->format());
    const uint32 *originalPixel = reinterpret_cast<const uint32*>(image->bits());
    uint32 *const generatedPixels = reinterpret_cast<uint32*>(generated.bits());
    for (int row=0; row < height; ++row) {
        for (int col=0; col < width; ++col) {
            int idx = col * height + row;
            generatedPixels[idx] = *originalPixel;
            ++originalPixel;
        }
    }
    *image = generated;
}

inline void rotate_right_mirror_vertical(QImage *const image) // rotate right->mirrored vertical
{
    const int height = image->height();
    const int width = image->width();
    QImage generated(/* width = */ height, /* height = */ width, image->format());
    const int lastCol = width - 1;
    const int lastRow = height - 1;
    const uint32 *pixel = reinterpret_cast<const uint32*>(image->bits());
    uint32 *const generatedBits = reinterpret_cast<uint32*>(generated.bits());
    for (int row=0; row < height; ++row) {
        for (int col=0; col < width; ++col) {
            int idx = (lastCol - col) * height + (lastRow - row);
            generatedBits[idx] = *pixel;
            ++pixel;
        }
    }
    *image = generated;
}

QTiffHandler::QTiffHandler() : QImageIOHandler()
{
    compression = NoCompression;
}

bool QTiffHandler::canRead() const
{
    if (canRead(device())) {
        setFormat("tiff");
        return true;
    }
    return false;
}

bool QTiffHandler::canRead(QIODevice *device)
{
    if (!device) {
        qWarning("QTiffHandler::canRead() called with no device");
        return false;
    }

    // current implementation uses TIFFClientOpen which needs to be
    // able to seek, so sequential devices are not supported
    QByteArray header = device->peek(4);
    return header == QByteArray::fromRawData("\x49\x49\x2A\x00", 4)
           || header == QByteArray::fromRawData("\x4D\x4D\x00\x2A", 4);
}

bool QTiffHandler::read(QImage *image)
{
    if (!canRead())
        return false;

    TIFF *const tiff = TIFFClientOpen("foo",
                                      "r",
                                      this,
                                      qtiffReadProc,
                                      qtiffWriteProc,
                                      qtiffSeekProc,
                                      qtiffCloseProc,
                                      qtiffSizeProc,
                                      qtiffMapProc,
                                      qtiffUnmapProc);

    if (!tiff) {
        return false;
    }
    uint32 width;
    uint32 height;
    uint16 photometric;
    if (!TIFFGetField(tiff, TIFFTAG_IMAGEWIDTH, &width)
        || !TIFFGetField(tiff, TIFFTAG_IMAGELENGTH, &height)
        || !TIFFGetField(tiff, TIFFTAG_PHOTOMETRIC, &photometric)) {
        TIFFClose(tiff);
        return false;
    }

    // BitsPerSample defaults to 1 according to the TIFF spec.
    uint16 bitPerSample;
    if (!TIFFGetField(tiff, TIFFTAG_BITSPERSAMPLE, &bitPerSample))
        bitPerSample = 1;
    uint16 samplesPerPixel; // they may be e.g. grayscale with 2 samples per pixel
    if (!TIFFGetField(tiff, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel))
        samplesPerPixel = 1;

    bool grayscale = photometric == PHOTOMETRIC_MINISBLACK || photometric == PHOTOMETRIC_MINISWHITE;
    if (grayscale && bitPerSample == 1 && samplesPerPixel == 1) {
        if (image->size() != QSize(width, height) || image->format() != QImage::Format_Mono)
            *image = QImage(width, height, QImage::Format_Mono);
        QVector<QRgb> colortable(2);
        if (photometric == PHOTOMETRIC_MINISBLACK) {
            colortable[0] = 0xff000000;
            colortable[1] = 0xffffffff;
        } else {
            colortable[0] = 0xffffffff;
            colortable[1] = 0xff000000;
        }
        image->setColorTable(colortable);

        if (!image->isNull()) {
            for (uint32 y=0; y<height; ++y) {
                if (TIFFReadScanline(tiff, image->scanLine(y), y, 0) < 0) {
                    TIFFClose(tiff);
                    return false;
                }
            }
        }
    } else {
        if ((grayscale || photometric == PHOTOMETRIC_PALETTE) && bitPerSample == 8 && samplesPerPixel == 1) {
            if (image->size() != QSize(width, height) || image->format() != QImage::Format_Indexed8)
                *image = QImage(width, height, QImage::Format_Indexed8);
            if (!image->isNull()) {
                const uint16 tableSize = 256;
                QVector<QRgb> qtColorTable(tableSize);
                if (grayscale) {
                    for (int i = 0; i<tableSize; ++i) {
                        const int c = (photometric == PHOTOMETRIC_MINISBLACK) ? i : (255 - i);
                        qtColorTable[i] = qRgb(c, c, c);
                    }
                } else {
                    // create the color table
                    uint16 *redTable = 0;
                    uint16 *greenTable = 0;
                    uint16 *blueTable = 0;
                    if (!TIFFGetField(tiff, TIFFTAG_COLORMAP, &redTable, &greenTable, &blueTable)) {
                        TIFFClose(tiff);
                        return false;
                    }
                    if (!redTable || !greenTable || !blueTable) {
                        TIFFClose(tiff);
                        return false;
                    }

                    for (int i = 0; i<tableSize ;++i) {
                        const int red = redTable[i] / 257;
                        const int green = greenTable[i] / 257;
                        const int blue = blueTable[i] / 257;
                        qtColorTable[i] = qRgb(red, green, blue);
                    }
                }

                image->setColorTable(qtColorTable);
                for (uint32 y=0; y<height; ++y) {
                    if (TIFFReadScanline(tiff, image->scanLine(y), y, 0) < 0) {
                        TIFFClose(tiff);
                        return false;
                    }
                }

                // free redTable, greenTable and greenTable done by libtiff
            }
        } else {
            if (image->size() != QSize(width, height) || image->format() != QImage::Format_ARGB32)
                *image = QImage(width, height, QImage::Format_ARGB32);
            if (!image->isNull()) {
                const int stopOnError = 1;
                if (TIFFReadRGBAImageOriented(tiff, width, height, reinterpret_cast<uint32 *>(image->bits()), ORIENTATION_TOPLEFT, stopOnError)) {
                    for (uint32 y=0; y<height; ++y)
                        convert32BitOrder(image->scanLine(y), width);
                } else {
                    TIFFClose(tiff);
                    return false;
                }
            }
        }
    }

    if (image->isNull()) {
        TIFFClose(tiff);
        return false;
    }

    float resX = 0;
    float resY = 0;
    uint16 resUnit;
    if (!TIFFGetField(tiff, TIFFTAG_RESOLUTIONUNIT, &resUnit))
        resUnit = RESUNIT_INCH;

    if (TIFFGetField(tiff, TIFFTAG_XRESOLUTION, &resX)
        && TIFFGetField(tiff, TIFFTAG_YRESOLUTION, &resY)) {

        switch(resUnit) {
        case RESUNIT_CENTIMETER:
            image->setDotsPerMeterX(qRound(resX * 100));
            image->setDotsPerMeterY(qRound(resY * 100));
            break;
        case RESUNIT_INCH:
            image->setDotsPerMeterX(qRound(resX * (100 / 2.54)));
            image->setDotsPerMeterY(qRound(resY * (100 / 2.54)));
            break;
        default:
            // do nothing as defaults have already
            // been set within the QImage class
            break;
        }
    }

    // rotate the image if the orientation is defined in the file
    uint16 orientationTag;
    if (TIFFGetField(tiff, TIFFTAG_ORIENTATION, &orientationTag)) {
        if (image->format() == QImage::Format_ARGB32) {
            // TIFFReadRGBAImageOriented() flip the image but does not rotate them
            switch (orientationTag) {
            case 5:
                rotate_right_mirror_horizontal(image);
                break;
            case 6:
                rotate_right_mirror_vertical(image);
                break;
            case 7:
                rotate_right_mirror_horizontal(image);
                break;
            case 8:
                rotate_right_mirror_vertical(image);
                break;
            }
        } else {
            switch (orientationTag) {
            case 1: // default orientation
                break;
            case 2: // mirror horizontal
                *image = image->mirrored(true, false);
                break;
            case 3: // mirror both
                *image = image->mirrored(true, true);
                break;
            case 4: // mirror vertical
                *image = image->mirrored(false, true);
                break;
            case 5: // rotate right mirror horizontal
                {
                    QMatrix transformation;
                    transformation.rotate(90);
                    *image = image->transformed(transformation);
                    *image = image->mirrored(true, false);
                    break;
                }
            case 6: // rotate right
                {
                    QMatrix transformation;
                    transformation.rotate(90);
                    *image = image->transformed(transformation);
                    break;
                }
            case 7: // rotate right, mirror vertical
                {
                    QMatrix transformation;
                    transformation.rotate(90);
                    *image = image->transformed(transformation);
                    *image = image->mirrored(false, true);
                    break;
                }
            case 8: // rotate left
                {
                    QMatrix transformation;
                    transformation.rotate(270);
                    *image = image->transformed(transformation);
                    break;
                }
            }
        }
    }


    TIFFClose(tiff);
    return true;
}

static bool checkGrayscale(const QVector<QRgb> &colorTable)
{
    if (colorTable.size() != 256)
        return false;

    const bool increasing = (colorTable.at(0) == 0xff000000);
    for (int i = 0; i < 256; ++i) {
        if ((increasing && colorTable.at(i) != qRgb(i, i, i))
            || (!increasing && colorTable.at(i) != qRgb(255 - i, 255 - i, 255 - i)))
            return false;
    }
    return true;
}

void QTiffHandler::convert32BitOrder(void *buffer, int width)
{
    uint32 *target = reinterpret_cast<uint32 *>(buffer);
    for (int32 x=0; x<width; ++x) {
        uint32 p = target[x];
        // convert between ARGB and ABGR
        target[x] = (p & 0xff000000)
                    | ((p & 0x00ff0000) >> 16)
                    | (p & 0x0000ff00)
                    | ((p & 0x000000ff) << 16);
    }
}

void QTiffHandler::convert32BitOrderBigEndian(void *buffer, int width)
{
    uint32 *target = reinterpret_cast<uint32 *>(buffer);
    for (int32 x=0; x<width; ++x) {
        uint32 p = target[x];
        target[x] = (p & 0xff000000) >> 24
                    | (p & 0x00ff0000) << 8
                    | (p & 0x0000ff00) << 8
                    | (p & 0x000000ff) << 8;
    }
}

QT_END_NAMESPACE
