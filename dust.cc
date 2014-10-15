/// \file dust.cc Automatic dust removal
#include "processed-source.h"
#include "inverse-attenuation.h"
#include "image-source-view.h"
#include "png.h"

struct CalibrationView : Application {
    ImageFolder folder1 {Folder("Pictures", home())};
    ImageFolder folder2 {Folder("Paper", folder1.folder)};
    Calibration calibration1 {folder1};
    Calibration calibration2 {folder2};
    ImageView views[2]  = {sRGB(calibration1.sum(folder1.maximumSize()/4)),sRGB(calibration2.sum(folder2.maximumSize()/4))};
    WidgetToggle toggleView {&views[0], &views[1]};
    Window window {&toggleView};
    CalibrationView() { log(withName(folder1.count(), folder2.count())); }
};
registerApplication(CalibrationView, calibration);

struct DustRemoval {
    Folder folder {"Pictures", home()};
    ImageFolder calibration {Folder("Paper", folder)};
    InverseAttenuation correction { calibration };
    ImageFolder source { folder,
                [](const String&, const map<String, String>& properties){ return fromDecimal(properties.at("Aperture"_)) <= 5; } };
    ProcessedSource corrected {source, correction};
};

struct DustRemovalTest : DustRemoval, Application {
    DustRemovalTest() { corrected.image(0, source.size(0), true); } // Calibration: 5s, Linear: 0.8s
};
registerApplication(DustRemovalTest, test);

struct DustRemovalPreview : DustRemoval, Application {
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

struct DustRemovalExport : DustRemoval, Application {
    DustRemovalExport() {
        Folder output ("Output", folder, true);
        for(size_t index: range(corrected.count())) {
            log(index,'/',corrected.count(), corrected.name(index));
            writeFile(corrected.name(index), encodePNG(corrected.image(index)), output);
            break;
        }
    }
};
registerApplication(DustRemovalExport, export);
