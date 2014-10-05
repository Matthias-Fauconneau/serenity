#include <exiv2/exiv2.hpp> //exiv2
#include "exif.h"
#include "string.h"
template<> String str(const std::string& s) { return String(string(s.data(), s.size())); }

map<String, Variant> parseExifTags(ref<byte> data) {
    map<String, Variant> tags;
    using namespace Exiv2;
    Image::AutoPtr image = ImageFactory::open((const uint8*)data.data, data.size);
    assert(image.get() != 0);
    image->readMetadata();
    for(Exifdatum e : image->exifData()) {
        String key = str(e.key());
        Variant value = 0;
        if(e.typeId()==unsignedRational || e.typeId()==signedRational) {
            //if(e.toRational().first == 0) continue;
            value = {e.toRational().first, e.toRational().second};
        }
        else if(e.typeId()!=undefined) { value = str(e.toString()); assert_(str(value).size <= 19, str(value), '\n', str(value).size, e.size(), e.typeName()); }
        tags.insertMulti(move(key), move(value));
    }
    return tags;
}
