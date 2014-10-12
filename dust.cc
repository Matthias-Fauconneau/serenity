/// \file dust.cc Automatic dust removal
#include "processed-source.h"
#include "inverse-attenuation.h"
#include "image-source-view.h"

struct DustRemovalPreview {
    Folder folder {"Pictures", home()};
    ImageFolder calibration {Folder("Paper", folder)};
    InverseAttenuation correction { calibration };
    ImageFolder source { folder,
                [](const String&, const map<String, String>& properties){ return fromDecimal(properties.at("Aperture"_)) <= 5; } };
    ProcessedSource corrected {source, correction};

    File last {".last", folder, Flags(ReadWrite|Create)};
    const size_t lastIndex = source.keys.indexOf(string(last.read(last.size())));
    size_t index = lastIndex != invalid ? lastIndex : 0;
    ~DustRemovalPreview() { last.seek(0); last.resize(last.write(str(source.name(index)))); }

    ImageSourceView sourceView {source, &index, window};
    ImageSourceView correctedView {corrected, &index, window};
    WidgetToggle toggleView {&sourceView, &correctedView};
    Window window {&toggleView};
} application;
