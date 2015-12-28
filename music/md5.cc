#include "md5.h"

//void MD5::operator()(const ref<int32> data) {
void MD5::operator()(const ref<float> data) {
#define big32 __builtin_bswap32
 for(size_t i=0; i<data.size; i+=16) {
  uint32 input[16] {
   big32(data[i+0]),big32(data[i+1]),big32(data[i+2]),big32(data[i+3]),
   big32(data[i+4]),big32(data[i+5]),big32(data[i+6]),big32(data[i+7]),
   big32(data[i+8]),big32(data[i+9]),big32(data[i+10]),big32(data[i+11]),
   big32(data[i+12]),big32(data[i+13]),big32(data[i+14]),big32(data[i+15])};
  uint32 a = digest[0];
  uint32 b = digest[1];
  uint32 c = digest[2];
  uint32 d = digest[3];

#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))
#define MD5STEP(f,w,x,y,z,input,s) (w += f(x,y,z) + input, w = (w<<s | w>>(32-s)) + x)

  MD5STEP(F1, a, b, c, d, input[0] + 0xd76aa478, 7);
  MD5STEP(F1, d, a, b, c, input[1] + 0xe8c7b756, 12);
  MD5STEP(F1, c, d, a, b, input[2] + 0x242070db, 17);
  MD5STEP(F1, b, c, d, a, input[3] + 0xc1bdceee, 22);
  MD5STEP(F1, a, b, c, d, input[4] + 0xf57c0faf, 7);
  MD5STEP(F1, d, a, b, c, input[5] + 0x4787c62a, 12);
  MD5STEP(F1, c, d, a, b, input[6] + 0xa8304613, 17);
  MD5STEP(F1, b, c, d, a, input[7] + 0xfd469501, 22);
  MD5STEP(F1, a, b, c, d, input[8] + 0x698098d8, 7);
  MD5STEP(F1, d, a, b, c, input[9] + 0x8b44f7af, 12);
  MD5STEP(F1, c, d, a, b, input[10] + 0xffff5bb1, 17);
  MD5STEP(F1, b, c, d, a, input[11] + 0x895cd7be, 22);
  MD5STEP(F1, a, b, c, d, input[12] + 0x6b901122, 7);
  MD5STEP(F1, d, a, b, c, input[13] + 0xfd987193, 12);
  MD5STEP(F1, c, d, a, b, input[14] + 0xa679438e, 17);
  MD5STEP(F1, b, c, d, a, input[15] + 0x49b40821, 22);

  MD5STEP(F2, a, b, c, d, input[1] + 0xf61e2562, 5);
  MD5STEP(F2, d, a, b, c, input[6] + 0xc040b340, 9);
  MD5STEP(F2, c, d, a, b, input[11] + 0x265e5a51, 14);
  MD5STEP(F2, b, c, d, a, input[0] + 0xe9b6c7aa, 20);
  MD5STEP(F2, a, b, c, d, input[5] + 0xd62f105d, 5);
  MD5STEP(F2, d, a, b, c, input[10] + 0x02441453, 9);
  MD5STEP(F2, c, d, a, b, input[15] + 0xd8a1e681, 14);
  MD5STEP(F2, b, c, d, a, input[4] + 0xe7d3fbc8, 20);
  MD5STEP(F2, a, b, c, d, input[9] + 0x21e1cde6, 5);
  MD5STEP(F2, d, a, b, c, input[14] + 0xc33707d6, 9);
  MD5STEP(F2, c, d, a, b, input[3] + 0xf4d50d87, 14);
  MD5STEP(F2, b, c, d, a, input[8] + 0x455a14ed, 20);
  MD5STEP(F2, a, b, c, d, input[13] + 0xa9e3e905, 5);
  MD5STEP(F2, d, a, b, c, input[2] + 0xfcefa3f8, 9);
  MD5STEP(F2, c, d, a, b, input[7] + 0x676f02d9, 14);
  MD5STEP(F2, b, c, d, a, input[12] + 0x8d2a4c8a, 20);

  MD5STEP(F3, a, b, c, d, input[5] + 0xfffa3942, 4);
  MD5STEP(F3, d, a, b, c, input[8] + 0x8771f681, 11);
  MD5STEP(F3, c, d, a, b, input[11] + 0x6d9d6122, 16);
  MD5STEP(F3, b, c, d, a, input[14] + 0xfde5380c, 23);
  MD5STEP(F3, a, b, c, d, input[1] + 0xa4beea44, 4);
  MD5STEP(F3, d, a, b, c, input[4] + 0x4bdecfa9, 11);
  MD5STEP(F3, c, d, a, b, input[7] + 0xf6bb4b60, 16);
  MD5STEP(F3, b, c, d, a, input[10] + 0xbebfbc70, 23);
  MD5STEP(F3, a, b, c, d, input[13] + 0x289b7ec6, 4);
  MD5STEP(F3, d, a, b, c, input[0] + 0xeaa127fa, 11);
  MD5STEP(F3, c, d, a, b, input[3] + 0xd4ef3085, 16);
  MD5STEP(F3, b, c, d, a, input[6] + 0x04881d05, 23);
  MD5STEP(F3, a, b, c, d, input[9] + 0xd9d4d039, 4);
  MD5STEP(F3, d, a, b, c, input[12] + 0xe6db99e5, 11);
  MD5STEP(F3, c, d, a, b, input[15] + 0x1fa27cf8, 16);
  MD5STEP(F3, b, c, d, a, input[2] + 0xc4ac5665, 23);

  MD5STEP(F4, a, b, c, d, input[0] + 0xf4292244, 6);
  MD5STEP(F4, d, a, b, c, input[7] + 0x432aff97, 10);
  MD5STEP(F4, c, d, a, b, input[14] + 0xab9423a7, 15);
  MD5STEP(F4, b, c, d, a, input[5] + 0xfc93a039, 21);
  MD5STEP(F4, a, b, c, d, input[12] + 0x655b59c3, 6);
  MD5STEP(F4, d, a, b, c, input[3] + 0x8f0ccc92, 10);
  MD5STEP(F4, c, d, a, b, input[10] + 0xffeff47d, 15);
  MD5STEP(F4, b, c, d, a, input[1] + 0x85845dd1, 21);
  MD5STEP(F4, a, b, c, d, input[8] + 0x6fa87e4f, 6);
  MD5STEP(F4, d, a, b, c, input[15] + 0xfe2ce6e0, 10);
  MD5STEP(F4, c, d, a, b, input[6] + 0xa3014314, 15);
  MD5STEP(F4, b, c, d, a, input[13] + 0x4e0811a1, 21);
  MD5STEP(F4, a, b, c, d, input[4] + 0xf7537e82, 6);
  MD5STEP(F4, d, a, b, c, input[11] + 0xbd3af235, 10);
  MD5STEP(F4, c, d, a, b, input[2] + 0x2ad7d2bb, 15);
  MD5STEP(F4, b, c, d, a, input[9] + 0xeb86d391, 21);

  digest[0] += a;
  digest[1] += b;
  digest[2] += c;
  digest[3] += d;
 }
}
