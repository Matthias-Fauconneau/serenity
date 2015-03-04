#include "jpeg.h"
#include "thread.h"

struct Cat {
	Cat() {
		auto files = currentWorkingDirectory().list(Files|Sorted);
		for(size_t index: range(files.size/2)) {
			array<Image> images = apply(files.slice(index*2, 2), [](string fileName) { return decodeImage(Map(fileName)); });
			int2 size = images[0].size;
			Image target(int2(images.size,1)*size);
			for(size_t index: range(images.size)) {
				for(size_t y: range(size.y)) {
					for(size_t x: range(size.x)) {
						target(index*size.x+x, y) = images[index](x, y);
					}
				}
			}
			writeFile(str(index)+".jpg", encodeJPEG(target), currentWorkingDirectory(), true);
		}
	}
} app;
