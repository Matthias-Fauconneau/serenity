/// \file crop.cc Automatic element removal by cropping
#include "serialization.h"
#include "processed-source.h"
#include "image-source-view.h"
#include "image-folder.h"

struct ElementRemoval {
	Folder folder {"Pictures/Crop", home()};
	//Crop correction;

	ImageFolder source { folder };
	//ProcessedSource corrected {source, correction};
};

struct SeedSelection : ImageSourceView {
	PersistentValue<map<String, String>> seeds {source.folder,"seeds"};

	using ImageSourceView::ImageSourceView;

	/// Sets seed position by clicking
	bool mouseEvent(int2 cursor, int2 size, Event event, Button button, Widget*& focus) override {
		log(int(event));
		if(event==Release) {
			seeds[source.name(index)] = str(cursor*source.size(index).x/size.x);
			log(seeds);
			return true;
		}
		return ImageSourceView::mouseEvent(cursor, size, event, button, focus);
	}
};

struct ElementRemovalPreview : ElementRemoval, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.name(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;

	SeedSelection sourceView {source, &index, window};
	ImageSourceView correctedView {source/*corrected*/, &index, window};
	WidgetToggle toggleView {&sourceView, &correctedView};
	Window window {&toggleView};
};
registerApplication(ElementRemovalPreview);
