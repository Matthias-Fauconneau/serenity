/// \file stitch.cc PanoramaStitch
#include "source-view.h"
#include "serialization.h"
#include "image-folder.h"
#include "operation.h"
#include "transform.h"

#include "layout.h"
#include "jpeg-encoder.h"

/// Layouts images
struct ImageLayout : ImageTransformGroupOperator, OperatorT<ImageLayout> {
	virtual array<Transform> operator()(ref<ImageF> images) const override {
		array<Transform> transforms;
		for(const ImageF& image : images) { // Stacks all images without any transform (FIXME)
			transforms.append(image.size, 0);
		}
		return transforms;
	}
};

struct LayoutBlur {
	Folder folder {"Documents/Pictures/LayoutBlur", home()};
	ImageFolder source { folder };
	AllImages groups {source};

	GroupImageOperation groupSource {source, groups};
	ImageGroupTransformOperationT<ImageLayout> transforms {groupSource};
	SampleImageGroupOperation layoutSource {transforms.source, transforms};
	ImageGroupFoldT<Sum> target {layoutSource}; // Sums images
};

struct LayoutBlurPreview : LayoutBlur, Application {
	PersistentValue<String> lastName {folder, ".last", [this]{ return source.elementName(index); }};
	const size_t lastIndex = source.keys.indexOf(lastName);
	size_t index = lastIndex != invalid ? lastIndex : 0;
	size_t imageIndex = 0;

	sRGBOperation sRGB [1] = {target};
	array<ImageSourceView> sRGBView = apply(mref<sRGBOperation>(sRGB),
													[&](ImageRGBSource& source) -> ImageSourceView { return {source, &index}; });
	VBox views {toWidgets(sRGBView), VBox::Share, VBox::Expand};
	Window window {&views, -1, [this]{ return views.title(); }};
};
registerApplication(LayoutBlurPreview);

struct LayoutBlurExport : LayoutBlur, Application {
	sRGBOperation sRGB {target};
	LayoutBlurExport() {
		Folder output ("Export", folder, true);
		for(size_t index: range(sRGB.count(-1))) {
			String name = sRGB.elementName(index);
			Time time;
			SourceImageRGB image = sRGB.image(index, int2(0,1680));
			time.stop();
			Time compressionTime;
			writeFile(name, encodeJPEG(image, 75), output, true);
			compressionTime.stop();
			log(str(100*(index+1)/sRGB.count(-1))+'%', '\t',index+1,'/',sRGB.count(-1),'\t',sRGB.elementName(index), strx(image.size),'\t',time);
		}
	}
};
registerApplication(LayoutBlurExport, export);

