kernel void add(global float* Y, const uint XY, const uint X, sampler_t sampler, const float alpha, read_only image3d_t A, const float beta, read_only image3d_t B) { // y = α a + β b
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = alpha * a + beta * b;
}

kernel void delta(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t A, read_only image3d_t B, read_only image3d_t C) { // y = c ? ( a - b ) / c : 0
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    float c = read_imagef(C, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = c ? ( a - b ) / c : 0;
}

kernel void update(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t A, const float alpha, read_only image3d_t B) { // y = max(0, a + b)
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = max(0.f, a + alpha * b);
}
