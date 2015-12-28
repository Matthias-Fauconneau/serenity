#pragma once
#include "core.h"

struct MD5 {
 uint32 digest[4] {0x67452301,0xefcdab89,0x98badcfe,0x10325476};
 void operator()(ref<int32> input);
 void operator()(ref<float> input);
};
