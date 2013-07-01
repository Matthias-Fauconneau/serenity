#include "view.h"
#include "text.h"
#include "interface.h"

class(TextView, View), virtual Text {
    bool view(const string&, const string&, const buffer<byte>& data) override {
        if(text || !isUTF8(data)) return false;
        //endsWith(metadata,"text"_ "label"_ "scalar"_ "size"_))
        setText(data);
        return text.size;
    }
};

class(ImageView, View), virtual ImageWidget {
    bool view(const string&, const string&, const buffer<byte>& data) override {
        if(image || !imageFileFormat(data)) return false;
        image = decodeImage(data);
        return image ? true : false;
    }
};
