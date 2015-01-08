/// \file dust.cc Automatic dust removal
#include "serialization.h"
#include "source-view.h"
#include "inverse-diffusion.h"
#include "source-view.h"
#include "export.h"

struct InverseAttenuationOperation : InverseAttenuation, ImageOperation {
	InverseAttenuationOperation(ImageRGBSource& calibration, ImageSource& source) : InverseAttenuation(calibration), ImageOperation(source, *this) {}
};

#if 0
struct CalibrationView : Application {
    ImageFolder folder1 {Folder("Pictures", home())};
    ImageFolder folder2 {Folder("Paper", folder1.folder)};
    Calibration calibration1 {folder1};
    Calibration calibration2 {folder2};
    ImageView views[2]  = {sRGB(calibration1.sum(folder1.maximumSize()/4)),sRGB(calibration2.sum(folder2.maximumSize()/4))};
	WidgetCycle toggleView {&views[0], &views[1]};
    Window window {&toggleView};
    CalibrationView() { log(withName(folder1.count(), folder2.count())); }
};
registerApplication(CalibrationView, calibration);
#endif

struct DustRemoval {
	Folder folder {"Documents/Pictures/DustRemoval", home()};
    ImageFolder calibration {Folder("Paper", folder)};
	ImageFolder source { folder };
	InverseAttenuationOperation target {calibration, source};
};

registerApplication(Export<DustRemoval>, export);

#if 0
struct DustRemovalTest : DustRemoval, Application {
	DustRemovalTest() { target.image(0, source.size(0), true); } // Calibration: 5s, Linear: 0.8s
};
registerApplication(DustRemovalTest, test);
#endif

#if 0
struct DustRemovalPreview : DustRemoval, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.name(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

    ImageSourceView sourceView {source, &index, window};
    ImageSourceView correctedView {corrected, &index, window};
    WidgetToggle toggleView {&sourceView, &correctedView};
	Window window {&toggleView, -1, [this]{ return toggleView.title()+" "+imagesAttributes.value(corrected.name(index)); } };
	DustRemovalPreview() {
		map<string, int> counts;
		for(string attribute: imagesAttributes.values) counts[attribute]++;
		log(counts);

		window.actions[Key('1')] = [this]{ setCurrentImageAttributes("bad"); }; // Bad correction
		window.actions[Key('2')] = [this]{ setCurrentImageAttributes("worse"); }; // Slightly worse than original
		window.actions[Key('3')] = [this]{ setCurrentImageAttributes("same"); }; // No correction needed
		window.actions[Key('4')] = [this]{ setCurrentImageAttributes("better"); }; // Slightly better than original
		window.actions[Key('5')] = [this]{ setCurrentImageAttributes("good"); }; // Good correction
		window.actions[Key('6')] = [this]{ setCurrentImageAttributes("best"); }; // Best corrections
	}

	void setCurrentImageAttributes(string currentImageAttributes) { imagesAttributes[corrected.name(index)] = String(currentImageAttributes); }
};
registerApplication(DustRemovalPreview);
#endif

#if 0
struct DustRemovalSource : DustRemoval, Application {
	DustRemovalSource() {
		Folder output ("Best", folder, true);
		for(size_t index: range(corrected.count())) {
			String name = source.name(index);
			SourceImageRGB image = source.image(index, int2(2048,1536), true);
			writeFile(name+".source", encodeJPEG(image, 75), output, true);
			log(str(100*(index+1)/corrected.count())+'%', '\t',index+1,'/',corrected.count(),
				'\t',imagesAttributes[corrected.name(index)],
				'\t',corrected.name(index), strx(corrected.size(index)));
		}
	}
};
registerApplication(DustRemovalSource, source);
#endif
