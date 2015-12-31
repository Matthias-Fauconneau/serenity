/// \file calibration.cc Flat field calibration
#include "raw.h"
#include "thread.h"
#include "data.h"
#include "map.h"
#include "tiff.h"
#include "IT8.h"

generic String diff(ref<T> a, ref<T> b) {
	assert_(a.size == b.size);
	array<char> s;
	for(size_t i: range(a.size)) {
		if(a[i] != b[i]) s.append(str(i)+'\t'+str(a[i])+'\t'+str(a[i], 16u, '0', 2u)+'\t'+str(b[i])+'\t'+str(b[i], 16u, '0', 2u)+'\n');
	}
	return move(s);
}

struct FlatFieldCalibration : Application {
	map<String, Image> images;
	FlatFieldCalibration() {
		Folder folder {"darkframes"};
		auto fileNames = folder.list(Files); fileNames.filter([](string fileName){return !endsWith(fileName,".raw16"); });
		buffer<uint16> lastRegisters;
		auto images = apply(fileNames, [&](string fileName) {
			Raw raw (Map(fileName, folder));
			log(withName(fileName, raw.gain, raw.gainDiv, raw.exposure, raw.temperature, mean(raw)));
			lastRegisters = copy(raw.registers);
			return move(raw);
		});
		if(images.size > 1) {
			ImageF c0 (Raw::size), c1 (Raw::size); // Affine fit dark energy = c0 + c1·exposureTime
			for(size_t pixelIndex: range(c0.Ref::size)) {
				// Direct evaluation of AtA and Atb
				real a00 = 0, a01 = 0, a11 = 0, b0 = 0, b1 = 0;
				for(const size_t constraintIndex: range(images.size)) {
					real x = images[constraintIndex].exposure; // c1 (·exposureTime)
					a00 += 1 * 1; // c0 (·1 constant)
					a01 += 1 * x;
					a11 += x * x;
					real b = images[constraintIndex][pixelIndex];
					b0 += 1 * b;
					b1 += x * b;
				}
				// Solves AtA x = Atb
				float det = a00*a11-a01*a01; // |AtA| (a10=a01)
				c0[pixelIndex] = (b0*a11 - a01*b1) / det;
				c1[pixelIndex] = (a00*b1 - b0*a01) / det;
			}
			writeFile("c0", cast<byte>(c0), currentWorkingDirectory(), true);
			writeFile("c1", cast<byte>(c1), currentWorkingDirectory(), true);
		} else {
			writeFile("c0", cast<byte>(images[0]), currentWorkingDirectory(), true);
		}
		mat4 rawRGBtosRGB = mat4(sRGB);
		if(1) {
			string it8FileName = arguments()[0];
			Raw raw {Map(it8FileName)};
			log(withName(it8FileName, raw.gain, raw.gainDiv, raw.exposure, raw.temperature, mean(raw)));
			log(diff(lastRegisters, raw.registers));
			IT8 it8(demosaic(raw), readFile("R100604.txt"));
			mat4 rawRGBtoXYZ = it8.rawRGBtoXYZ;
			rawRGBtosRGB = mat4(sRGB) * rawRGBtoXYZ;
			//this->images.insert("chart"__, convert(mix(convert(it8.chart, rawRGBtosRGB), it8.spotsView)));
		}
		this->images = {move(fileNames), apply(images, [&](const ImageF& raw) { return convert(convert(demosaic(raw), rawRGBtosRGB)); })};
	}
};

#if 1
#include "view.h"
struct Preview : FlatFieldCalibration, WindowCycleView { Preview() : WindowCycleView(images) {} };
registerApplication(Preview);
#endif

#if 0
#include "demosaic.h"
#include "png.h"

generic void multiply(mref<T> Y, mref<T> X, T c) { for(size_t i: range(Y.size)) Y[i] = c * X[i]; }
ImageF normalize(ImageF&& target, const ImageF& source) { multiply(target, source, 1/mean(source)); return move(target); }
ImageF normalize(const ImageF& source) { return normalize(source.size, source); }

struct Visualization {
	Visualization() {
		Map map("c0");
		ImageF c0 (unsafeRef(cast<float>(map)), Raw::size);
		writeFile("c0.png", encodePNG(convert(demosaic(normalize(c0/*subtract(c0, min(c0))*/)))), currentWorkingDirectory(), true);
		writeFile("c1.png", encodePNG(convert(demosaic(normalize(ImageF(unsafeRef(cast<float>(Map("c1"))), Raw::size))))), currentWorkingDirectory(), true);
	}
} app;
#endif
