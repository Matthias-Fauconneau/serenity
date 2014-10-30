#include "image.h"
#include "data.h"
#include "vector.h"
#include "parallel.h"
#include "math.h"
#include "map.h"

// -- Decode --

string imageFileFormat(const ref<byte> file) {
    if(startsWith(file,"\xFF\xD8")) return "JPEG";
    else if(startsWith(file,"\x89PNG\r\n\x1A\n")) return "PNG";
    else if(startsWith(file,"\x00\x00\x01\x00")) return "ICO";
    else if(startsWith(file,"\x49\x49\x2A\x00") || startsWith(file,"\x4D\x4D\x00\x2A")) return "TIFF";
    else if(startsWith(file,"BM")) return "BMP";
    else return "";
}

int2 imageSize(const ref<byte> file) {
    BinaryData s(file, true);
    if(s.match(ref<uint8>{0b10001001,'P','N','G','\r','\n',0x1A,'\n'})) {
        for(;;) {
            s.advance(4); // Length
            if(s.read<byte>(4) == "IHDR"_) {
                uint width = s.read(), height = s.read();
                return int2(width, height);
            }
        }
    }
    enum Marker : uint8 {
        StartOfFrame = 0xC0, DefineHuffmanTable = 0xC4, StartOfImage = 0xD8, EndOfImage = 0xD9,
        StartOfSlice = 0xDA, DefineQuantizationTable = 0xDB, DefineRestartInterval = 0xDD, ApplicationSpecific = 0xE0 };
    if(s.match(ref<uint8>{0xFF, StartOfImage})) {
        for(;;){
            s.skip((uint8)0xFF);
            uint8 marker = s.read();
            if(marker == EndOfImage) break;
            if(marker==StartOfSlice) {
                while(s.available(2) && ((uint8)s.peek() != 0xFF || uint8(s.peek(2)[1])<0xC0)) s.advance(1);
            } else {
                uint16 length = s.read(); // Length
                if(marker==StartOfFrame) {
                    uint8 precision = s.read(); assert_(precision==8);
                    uint16 height = s.read();
                    uint16 width = s.read();
                    return int2(width, height);
                    //uint8 components = s.read();
                    //for(components) { ident:8, h_samp:4, v_samp:4, quant:8 }
                } else s.advance(length-2);
            }
        }
    }
    error("Unknown image format", hex(file.size<16?file:s.peek(16)));
}

Image  __attribute((weak)) decodePNG(const ref<byte>) { error("PNG support not linked"); }
Image  __attribute((weak)) decodeJPEG(const ref<byte>) { error("JPEG support not linked"); }
Image  __attribute((weak)) decodeICO(const ref<byte>) { error("ICO support not linked"); }
Image  __attribute((weak)) decodeTIFF(const ref<byte>) { error("TIFF support not linked"); }
Image  __attribute((weak)) decodeBMP(const ref<byte>) { error("BMP support not linked"); }
Image  __attribute((weak)) decodeTGA(const ref<byte>) { error("TGA support not linked"); }

Image decodeImage(const ref<byte> file) {
    if(startsWith(file,"\xFF\xD8")) return decodeJPEG(file);
    else if(startsWith(file,"\x89PNG")) return decodePNG(file);
    else if(startsWith(file,"\x00\x00\x01\x00")) return decodeICO(file);
    else if(startsWith(file,"\x00\x00\x02\x00")||startsWith(file,"\x00\x00\x0A\x00")) return decodeTGA(file);
    else if(startsWith(file,"\x49\x49\x2A\x00") || startsWith(file,"\x4D\x4D\x00\x2A")) return decodeTIFF(file);
    else if(startsWith(file,"BM")) return decodeBMP(file);
    else { if(file.size) error("Unknown image format", hex(file.slice(0,min<int>(file.size,4)))); return Image(); }
}

// -- Rotate --

void rotate(const Image& target, const Image& source) {
	for(int y: range(source.height)) for(int x: range(source.width)) target(source.height-1-y, x) = source(x,y);
}

// -- Resample (3x8bit) --

static void box(const Image& target, const Image& source) {
    assert_(!source.alpha); //FIXME: not alpha correct
	int scale = source.width/target.width;
	assert_(scale <= 256, target.size, source.size);
	assert_((target.size-int2(1))*scale+int2(scale-1) < source.size);
    chunk_parallel(target.height, [&](uint, size_t y) {
        const byte4* sourceLine = source.data + y * scale * source.stride;
        byte4* targetLine = target.begin() + y * target.stride;
        for(uint unused x: range(target.width)) {
			const byte4* sourceSpanOrigin = sourceLine + x * scale;
            uint4 s = 0;
            for(uint i: range(scale)) {
                const byte4* sourceSpan = sourceSpanOrigin + i * source.stride;
                for(uint j: range(scale)) s += uint4(sourceSpan[j]);
            }
            s /= scale*scale;
            targetLine[x] = byte4(s[0], s[1], s[2], 0xFF);
        }
    });
}
static Image box(Image&& target, const Image& source) { box(target, source); return move(target); }

static void bilinear(const Image& target, const Image& source) {
    assert_(!source.alpha);
    const uint stride = source.stride;
    chunk_parallel(target.height, [&](uint, size_t y) {
        for(uint x: range(target.width)) {
            const uint fx = x*256*(source.width-1)/target.width, fy = y*256*(source.height-1)/target.height; //TODO: incremental
            uint ix = fx/256, iy = fy/256;
            uint u = fx%256, v = fy%256;
            const ref<byte4> span = source.slice(iy*stride+ix);
			byte4 d;
            for(int i=0; i<3; i++) { // Interpolates values as if in linear space (not sRGB)
                d[i] = ((uint(span[      0][i]) * (256-u) + uint(span[           1][i]) * u) * (256-v)
                       + (uint(span[stride][i]) * (256-u) + uint(span[stride+1][i]) * u) * (       v) ) / (256*256);
            }
            d[3] = 0xFF;
            target(x, y) = d;
        }
    });
}

void resize(const Image& target, const Image& source) {
    assert_(source && target && target.size != source.size, source.size, target.size);
    if(source.width%target.width==0 && source.height%target.height==0) box(target, source); // Integer box downsample
    else if(target.size > source.size/2) bilinear(target, source); // Bilinear resample
    else { // Integer box downsample + Bilinear resample
        int downsampleFactor = min(source.size.x/target.size.x, source.size.y/target.size.y);
		bilinear(target, box((source.size/*+int2((downsampleFactor-1)/2)*/)/downsampleFactor, source));
    }
}

// -- sRGB --

uint8 sRGB_forward[0x1000];  // 4K (FIXME: interpolation of a smaller table might be faster)
void __attribute((constructor(1001))) generate_sRGB_forward() {
    for(uint index: range(sizeof(sRGB_forward))) {
        real linear = (real) index / (sizeof(sRGB_forward)-1);
        real sRGB = linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear;
        assert(abs(linear-(sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92))<exp2(-50));
        sRGB_forward[index] = round(0xFF*sRGB);
    }
}

float sRGB_reverse[0x100];
void __attribute((constructor(1001))) generate_sRGB_reverse() {
    for(uint index: range(0x100)) {
        real sRGB = (real) index / 0xFF;
        real linear = sRGB > 0.04045 ? pow((sRGB+0.055)/1.055, 2.4) : sRGB / 12.92;
        assert(abs(sRGB-(linear > 0.0031308 ? 1.055*pow(linear,1/2.4)-0.055 : 12.92*linear))<exp2(-50));
        sRGB_reverse[index] = linear;
        assert(sRGB_forward[int(round(0xFFF*sRGB_reverse[index]))]==index);
    }
}

void linear(mref<float> target, ref<byte4> source, int component) {
    /***/ if(component==0) parallel::apply(target, [](byte4 sRGB) { return sRGB_reverse[sRGB[0]]; }, source);
    else if(component==1) parallel::apply(target, [](byte4 sRGB) { return sRGB_reverse[sRGB[1]]; }, source);
    else if(component==2) parallel::apply(target, [](byte4 sRGB) { return sRGB_reverse[sRGB[2]]; }, source);
    else error(component);
}

static uint8 sRGB(float v) {
    assert_(isNumber(v), v);
    v = ::min(1.f, v); // Saturates
    v = ::max(0.f, v); // Clips
    uint linear12 = 0xFFF*v;
    assert_(linear12 < 0x1000);
    return sRGB_forward[linear12];
}
void sRGB(mref<byte4> target, ref<float> source) {
    parallel::apply(target, [](float value) { uint8 v=sRGB(value); return byte4(v,v,v, 0xFF); }, source);
}
void sRGB(mref<byte4> target, ref<float> blue, ref<float> green, ref<float> red) {
	parallel::apply(target, [=](float b, float g, float r) { return byte4(sRGB(b), sRGB(g), sRGB(r), 0xFF); }, blue, green, red);
}

// -- Resampling (float) --

ImageF downsample(ImageF&& target, const ImageF& source) {
    assert_(target.size == source.size/2, target.size, source.size);
    for(uint y: range(target.height)) for(uint x: range(target.width))
        target(x,y) = (source(x*2+0,y*2+0) + source(x*2+1,y*2+0) + source(x*2+0,y*2+1) + source(x*2+1,y*2+1)) / 4;
    return move(target);
}

ImageF resize(ImageF&& target, ImageF&& source) {
    assert_(source.width*target.height==source.height*target.width); // Uniform scale
    assert_(source.size > target.size, target.size, source.size); // Downsample
    assert_(source.size.x%target.size.x == 0, target.size, source.size); // Integer ratio
    assert_(isPowerOfTwo(source.size.x/target.size.x)); // Mipmap downsample
    int times = log2(uint(source.size.x/target.size.x));
    ImageF inplaceSource = share(source);
    for(uint unused iteration: range(times-1)) {
        ImageF inplaceTarget = share(inplaceSource); inplaceTarget.size = inplaceSource.size/2;
        inplaceTarget = downsample(move(inplaceTarget), inplaceSource);
        inplaceSource = move(inplaceTarget);
    }
    return downsample(move(target), inplaceSource);
}

// -- Convolution --

/// Convolves and transposes (with mirror border conditions)
static void convolve(float* target, const float* source, const float* kernel, int radius, int width, int height, uint sourceStride, uint targetStride) {
    int N = radius+1+radius;
	assert_(N < 1024, N);
    chunk_parallel(height, [=](uint, size_t y) {
        const float* line = source + y * sourceStride;
        float* targetColumn = target + y;
		if(width >= radius+1) {
			for(int x: range(-radius,0)) {
				float sum = 0;
				for(int dx: range(N)) sum += kernel[dx] * line[abs(x+dx)];
				targetColumn[(x+radius)*targetStride] = sum;
			}
			for(int x: range(0,width-2*radius)) {
				float sum = 0;
				const float* span = line + x;
				for(int dx: range(N)) sum += kernel[dx] * span[dx];
				targetColumn[(x+radius)*targetStride] = sum;
			}
			for(int x: range(width-2*radius,width-radius)){
				float sum = 0;
				for(int dx: range(N)) sum += kernel[dx] * line[width-1-abs(x+dx-(width-1))];
				targetColumn[(x+radius)*targetStride] = sum;
			}
		} else {
			for(int x: range(-radius, width-radius)) {
				float sum = 0;
				for(int dx: range(N)) sum += kernel[dx] * line[width-1-abs(abs(x+dx)-(width-1))];
				targetColumn[(x+radius)*targetStride] = sum;
			}
		}
    });
}

void gaussianBlur(const ImageF& target, const ImageF& source, float sigma, int radius) {
    assert_(sigma > 0);
	if(!radius) radius = ceil(3*sigma);
    size_t N = radius+1+radius;
	assert_(int2(radius+1) <= source.size, sigma, radius, N, source.size);
    float kernel[N];
    for(int dx: range(N)) kernel[dx] = gaussian(sigma, dx-radius); // Sampled gaussian kernel (FIXME)
	real sum = ::sum<real>(ref<float>(kernel,N)); assert_(sum, ref<float>(kernel,N)); mref<float>(kernel,N) *= 1/sum;
    buffer<float> transpose (target.height*target.width, "transpose");
    convolve(transpose.begin(), source.begin(), kernel, radius, source.width, source.height, source.stride, source.height);
    assert_(source.size == target.size);
    convolve(target.begin(),  transpose.begin(), kernel, radius, target.height, target.width, target.height, target.stride);
}
