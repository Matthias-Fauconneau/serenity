/// \file blend.cc Automatic exposure blending
#include "serialization.h"
#include "image-folder.h"
#include "processed-source.h"
#include "normalize.h"
#include "difference.h"
#include "image-source-view.h"

struct ExposureBlend {
	Folder folder {"Pictures/ExposureBlend", home()};
	PersistentValue<map<String, String>> imagesAttributes {folder,"attributes"};
	ImageFolder source { folder };
	ProcessedSourceT<Normalize> normalize {source};
	ProcessedSequenceT<Difference> difference {normalize};
};

struct ExposureBlendPreview : ExposureBlend, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.name(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	ImageSourceView sourceView {source, &index, window};
	ImageSourceView processedView {difference, &index, window};
	WidgetToggle toggleView {&sourceView, &processedView};
	Window window {&toggleView, -1, [this]{ return toggleView.title()+" "+imagesAttributes.value(source.name(index)); }};

	ExposureBlendPreview() {
		for(char c: range('0','9'+1)) window.actions[Key(c)] = [this, c]{ setCurrentImageAttributes("#"_+c); };
	}
	void setCurrentImageAttributes(string currentImageAttributes) { imagesAttributes[source.name(index)] = String(currentImageAttributes); }
};
registerApplication(ExposureBlendPreview);

struct ExposureBlendTest : ExposureBlend, Application {
	ExposureBlendTest() {
		int setSize = 0;
		for(size_t index: range(difference.count())) {
			setSize++;
			String name = difference.name(index);
			SourceImage image = difference.image(index, 0, int2(1024,768));
			float MSE = sqrt(parallel::energy(image)/image.ref::size);
			log(index+1,"/",difference.count(), name, MSE, imagesAttributes[source.name(index)]);
			const float threshold = 0.41;
			if(setSize>1 && MSE > threshold) { log("-"); setSize=0; } // Set break
		}
	}
};
registerApplication(ExposureBlendTest, test);
