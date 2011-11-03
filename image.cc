#include "image.h"
#include "vector.h"
#include "string.h"
#include <zlib.h>

Image::Image(uint8* file, int size) {
	Stream s(file,size);
	assert(s.match(_("\x89PNG\r\n\x1A\n")));
	z_stream z; clear(z); inflateInit(&z);
	string idat(size*16); //FIXME
	z.next_out = (Bytef*)idat.data, z.avail_out = (uint)idat.capacity;
	while(s) {
		uint32 size = s.read();
		string name = s.read(4);
		if(name == _("IHDR")) {
			width = (int)(uint32)s.read(), height = (int)(uint32)s.read();
			uint8 bitDepth = s.read(), type = s.read(), compression = s.read(), filter = s.read(), interlace = s.read();
			assert(bitDepth==8); assert(compression==0); assert(filter==0); assert(interlace==0);
			depth = "\x01\x00\x03\x00\x02\x00\x04"[type];
		} else if(name == _("IDAT")) {
			z.avail_in = size;
			z.next_in = (Bytef*)s.read((int)size).data;
			inflate(&z, Z_NO_FLUSH);
		} else s += (int)size;
		s+=4; //CRC
	}
	inflate(&z, Z_FINISH);
	inflateEnd(&z);
	idat.size = (int)z.total_out;
	int stride = width*depth;
	assert(idat.size == height*(1+stride), idat.size, width, height);
	assert(depth==2);
	data = new uint8[height*stride];
	uint8* raw = (uint8*)idat.data;
	byte2* dst = (byte2*)data;
	byte2 zero[width]; clear(zero,width); byte2* prior=zero;
	for(int y=0;y<height;y++,raw+=stride,dst+=width) {
		int filter = *raw++; assert(filter>=0 && filter<=4);
		byte2* src = (byte2*)raw;
		byte2 a;
		if(filter==0) copy(dst,src,width);
		if(filter==1) for(int i=0;i<width;i++) dst[i]= a= a+src[i];
		if(filter==2) for(int i=0;i<width;i++) dst[i]= prior[i]+src[i];
		if(filter==3) for(int i=0;i<width;i++) dst[i]= a= byte2((int2(prior[i])+int2(a))/2)+src[i];
		if(filter==4) {
			byte2 a; int2 b;
			for(int i=0;i<width;i++) {
				int2 c = b;
				b = int2(prior[i]);
				int2 d = int2(a) + b - c;
				int2 pa = abs(d-int2(a)), pb = abs(d-b), pc = abs(d-c);
				byte2 p; for(int i=0;i<2;i++) p[i]=uint8(pa[i] <= pb[i] && pa[i] <= pc[i] ? a[i] : pb[i] <= pc[i] ? b[i] : c[i]);
				dst[i]= a= p+src[i];
			}
		}
		prior = dst;
	}
}
