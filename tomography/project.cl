__kernel void project(__global float* image, __global const float* volume, const uint width) {
 size_t x = get_global_id(0);
 size_t y = get_global_id(1);
 size_t i = y * 504 + x;
 image[i] = x;
}
