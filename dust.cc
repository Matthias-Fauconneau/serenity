/// \file dust.cc Automatic dust removal
#include "processed-source.h"
#include "inverse-attenuation.h"
#include "image-source-view.h"

struct DustRemoval {
    Folder folder {"Pictures", home()};
    ImageFolder calibration {Folder("Paper", folder)};
    InverseAttenuation correction { calibration };
    ImageFolder source { folder,
                [](const String&, const map<String, String>& properties){ return fromDecimal(properties.at("Aperture"_)) <= 5; } };
};

struct CalibrationView : DustRemoval, Application {
    ImageView views[2]  = {sRGB(correction.sum(source.maximumSize()/4)),sRGB(correction.attenuation(source.maximumSize()/4))};
    WidgetToggle toggleView {&views[0], &views[1]};
    Window window {&toggleView};
};
registerApplication(CalibrationView, calibration);

struct DustRemovalTest : DustRemoval, Application {
    ProcessedSource corrected {source, correction};
    DustRemovalTest() { corrected.image(0, source.size(0), true); } // Calibration: 5s, Linear: 0.8s
};
registerApplication(DustRemovalTest, test);

struct DustRemovalPreview : DustRemoval, Application {
    ProcessedSource corrected {source, correction};

    File last {".last", folder, Flags(ReadWrite|Create)};
    const size_t lastIndex = source.keys.indexOf(string(last.read(last.size())));
    size_t index = lastIndex != invalid ? lastIndex : 0;
    ~DustRemovalPreview() { last.seek(0); last.resize(last.write(str(source.name(index)))); }

    ImageSourceView sourceView {source, &index, window};
    ImageSourceView correctedView {corrected, &index, window};
    WidgetToggle toggleView {&sourceView, &correctedView};
    Window window {&toggleView};
};
registerApplication(DustRemovalPreview);
