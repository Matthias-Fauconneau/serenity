kernel void divdiff(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t A, read_only image3d_t B, read_only image3d_t C) { // Division of difference: y = c ? ( a - b ) / c : 0 [SIRT]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    float c = read_imagef(C, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = c ? ( a - b ) / c : 0;
}

kernel void maxadd(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t A, const float alpha, read_only image3d_t B) { // Maximum of zero and addition: y = max(0, a + α b) [SIRT, CG]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = max(0.f, a + alpha * b);
}

kernel void add(global float* Y, const uint XY, const uint X, sampler_t sampler, const float alpha, read_only image3d_t A, const float beta, read_only image3d_t B) { // Weighted addition: y = α a + β b [CG, MLTR]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = alpha * a + beta * b;
}

kernel void mulexp(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t A, read_only image3d_t B) { // Multiplication with exponential: y = a exp(-b) [MLTR]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = a * exp(-b);
}

kernel void diffexp(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t A, read_only image3d_t B) { // Difference with exponential: y = exp(-a) - exp(-b) [MLTR]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = exp(-a) - exp(-b);
}

kernel void adddiv(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t A, read_only image3d_t B, read_only image3d_t C) { // Difference with exponential: y = max(0, a + c ? b / c : 0) [MLTR]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    float c = read_imagef(C, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = max(0.f, a + c ? b / c : 0);
}
