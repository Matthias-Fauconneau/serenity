#include "image-folder.h"
#include "png.h"

/// Returns the image file format if valid
string imageFileFormat(const ref<byte> file) {
 if(startsWith(file,"\xFF\xD8"_)) return "JPEG"_;
 else if(startsWith(file,"\x89PNG\r\n\x1A\n"_)) return "PNG"_;
 else if(startsWith(file,"\x00\x00\x01\x00"_)) return "ICO"_;
 else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return "TIFF"_;
 else if(startsWith(file,"BM"_)) return "BMP"_;
 else return ""_;
}

int2 imageSize(const ref<byte> file) {
 BinaryData s(file, true);
 // PNG
 if(s.match(ref<uint8>{0b10001001,'P','N','G','\r','\n',0x1A,'\n'})) {
  for(;;) {
   s.advance(4); // Length
   if(s.read<byte>(4) == "IHDR"_) {
    uint width = s.read(), height = s.read();
    return int2(width, height);
   }
  }
  error("PNG");
 }
 // JPEG
 enum Marker : uint8 {
  StartOfFrame = 0xC0, DefineHuffmanTable = 0xC4, StartOfImage = 0xD8, EndOfImage = 0xD9,
  StartOfSlice = 0xDA, DefineQuantizationTable = 0xDB, DefineRestartInterval = 0xDD, ApplicationSpecific = 0xE0 };
 if(s.match(ref<uint8>{0xFF, StartOfImage})) {
  for(;;){
   s.skip((uint8)0xFF);
   uint8 marker = s.read();
   if(marker == EndOfImage) break;
   if(marker==StartOfSlice) {
    while(s.available(2) && ((uint8)s.peek() != 0xFF || uint8(s.peek(2)[1])<0xC0)) s.advance(1);
   } else {
    uint16 length = s.read(); // Length
    if(marker>=StartOfFrame && marker<=StartOfFrame+2) {
     uint8 precision = s.read(); assert_(precision==8);
     uint16 height = s.read();
     uint16 width = s.read();
     return int2(width, height);
     //uint8 components = s.read();
     //for(components) { ident:8, h_samp:4, v_samp:4, quant:8 }
    } else s.advance(length-2);
   }
  }
  error("JPG");
 }
 error("Unknown image format", hex(file.size<16?file:s.peek(16)));
}

__attribute((weak)) Image decodePNG(const ref<byte>) { error("PNG support not linked"); }
__attribute((weak)) Image decodeJPEG(const ref<byte>) { error("JPEG support not linked"); }
__attribute((weak)) Image decodeICO(const ref<byte>) { error("ICO support not linked"); }
__attribute((weak)) Image decodeTIFF(const ref<byte>) { error("TIFF support not linked"); }
__attribute((weak)) Image decodeBMP(const ref<byte>) { error("BMP support not linked"); }
__attribute((weak)) Image decodeTGA(const ref<byte>) { error("TGA support not linked"); }

Image decodeImage(const ref<byte> file) {
 if(!file) return Image();
 else if(startsWith(file,"\xFF\xD8"_)) return decodeJPEG(file);
 else if(startsWith(file,"\x89PNG"_)) return decodePNG(file);
 else if(startsWith(file,"\x00\x00\x01\x00"_)) return decodeICO(file);
 else if(startsWith(file,"\x00\x00\x02\x00"_)||startsWith(file,"\x00\x00\x0A\x00"_)) return decodeTGA(file);
 else if(startsWith(file,"\x49\x49\x2A\x00"_) || startsWith(file,"\x4D\x4D\x00\x2A"_)) return decodeTIFF(file);
 else if(startsWith(file,"BM"_)) return decodeBMP(file);
 else error("Unknown image format", hex(file.slice(0,min<int>(file.size,4))));
}

ImageFolder::ImageFolder(const Folder& source, function<bool(string name, const map<string, String>& properties)> predicate, const int downsample)
    : source(Folder(".",source)), downsample(downsample) {
    {// Lists images and their properties
        for(String& fileName: source.list(Files|Sorted)) {
            Map file = Map(fileName, source);
            if(!ref<string>{"JPEG"_,"PNG"_}.contains(imageFileFormat(file))) continue; // Only JPEG and PNG images
            int2 imageSize = ::imageSize(file);

            map<String, Variant> exif = parseExifTags(file);

            if(exif.contains("Exif.Image.Orientation"_) && (string)exif.at("Exif.Image.Orientation"_) == "6") imageSize = int2(imageSize.y, imageSize.x);
            Variant date = ""_;
            if(exif.contains("Exif.Image.DateTime"_)) date = move(exif.at("Exif.Image.DateTime"));
            date = section((string)date, ' '); // Discards time to group by date using PropertyGroup
            if(exif.contains("Exif.Photo.ExposureTime"_)) exif.at("Exif.Photo.ExposureTime"_).number *= 1000; // Scales seconds to milliseconds

            map<string, String> properties;
            properties.insert("Size", strx(imageSize));
            properties.insert("Path", copy(fileName));
            if(exif.contains("Exif.Image.DateTime"_)) properties.insert("Date", str(date));
            if(exif.contains("Exif.Image.Orientation"_)) properties.insert("Orientation", str(exif.at("Exif.Image.Orientation"_)));
            if(exif.contains("Exif.Photo.FocalLength"_)) properties.insert("Focal", str(exif.at("Exif.Photo.FocalLength"_)));
            if(exif.contains("Exif.Photo.FNumber"_)) properties.insert("Aperture", str(exif.at("Exif.Photo.FNumber"_)));
            if(exif.contains("Exif.Photo.ExposureBiasValue"_)) properties.insert("Bias", str(exif.at("Exif.Photo.ExposureBiasValue"_)));
            if(exif.contains("Exif.Photo.ISOSpeedRatings"_)) properties.insert("Gain", str(exif.at("Exif.Photo.ISOSpeedRatings"_)));
            if(exif.contains("Exif.Photo.ExposureTime"_)) properties.insert("Time", str(exif.at("Exif.Photo.ExposureTime"_)));

            string name = section(fileName,'.');
            if(predicate && predicate(name, properties)) continue;

            insert(copyRef(name), move(properties));

            maximumImageSize = max(maximumImageSize, imageSize);
        }
    }
    // Applies application specific filter
    if(predicate) filter(predicate);
}

/// Converts encoded sRGB images to raw (mmap'able) sRGB images
SourceImageRGB ImageFolder::image(size_t index, string) {
    assert_(index  < count());
    File sourceFile (values[index].at("Path"_), source);
    return cache<Image>(path()+"/Source", elementName(index), parse<int2>(values[index].at("Size"_)), sourceFile.modifiedTime(), [&](const Image& target) {
        Image source = decodeImage(Map(sourceFile));
        assert_(source.size);
        if(values[index].contains("Orientation") && values[index].at("Orientation") == "6") error("rotate(target, source);");
        else target.copy(source);
    }, true /*Disables full size source cache*/, "" /*Disables version invalidation to avoid redecoding on header changes*/);
}

/// Resizes sRGB images
/// \note Resizing after linear float conversion would be more accurate but less efficient
SourceImageRGB ImageFolder::image(size_t index, int2 hint, string parameters) {
    assert_(index  < count());
    File sourceFile (values[index].at("Path"_), source);
    int2 sourceSize = parse<int2>(values[index].at("Size"_));
    int2 size = this->size(index, hint);
    if(size==sourceSize) return image(index, parameters);
    return cache<Image>(path()+"/Resize", elementName(index), size, sourceFile.modifiedTime(), [&](const Image& target) {
        SourceImageRGB source = image(index);
        assert_(size==target.size && size >= int2(12) && size <= source.size, size, hint);
        assert_(target.stride%8==0);
        resize(target, source);
    }, false, "" /*Disables version invalidation to avoid redecoding and resizing on header changes*/);
}

/// Converts sRGB images to linear float images
SourceImage ImageFolder::image(size_t index, size_t componentIndex, int2 size, string parameters) {
    assert_(index  < count());
    int2 targetSize = size?:this->size(index);
    return cache<ImageF>(path()+"/Linear["+str(componentIndex)+']', elementName(index), targetSize, time(index),
                         [&](const ImageF& target) {
        SourceImageRGB source = image(index, targetSize, parameters);
        assert_(source.size == target.size, source.size, target.size, size, targetSize);
        linear(target, source, componentIndex);
    } );
}
