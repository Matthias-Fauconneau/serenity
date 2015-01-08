#pragma once
#include "operation.h"
#include "thread.h"
#include "jpeg-encoder.h"

generic struct Export : T, Application {
	sRGBOperation sRGB {T::target};
	Export() {
		Folder output ("Export", T::folder, true);
		for(size_t index: range(sRGB.count(-1))) {
			String name = sRGB.elementName(index);
			Time correctionTime;
			SourceImageRGB image = sRGB.image(index);
			correctionTime.stop();
			Time compressionTime;
			writeFile(name, encodeJPEG(image), output, true);
			compressionTime.stop();
			log(str(100*(index+1)/sRGB.count(-1))+'%', '\t',index+1,'/',sRGB.count(-1),
				'\t',sRGB.elementName(index),
				'\t',correctionTime, compressionTime);
		}
	}
};
