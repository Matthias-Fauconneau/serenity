kernel void delta(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t A, read_only image3d_t B, read_only image3d_t C) { // y = c ? ( a - b ) / c : 0 [SIRT]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    float c = read_imagef(C, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = c ? ( a - b ) / c : 0;
}

kernel void update(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t A, const float alpha, read_only image3d_t B) { // y = max(0, a + b) [SIRT, CG]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = max(0.f, a + alpha * b);
}

kernel void add(global float* Y, const uint XY, const uint X, sampler_t sampler, const float alpha, read_only image3d_t A, const float beta, read_only image3d_t B) { // y = α a + β b [CG]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, sampler, i).x;
    float b = read_imagef(B, sampler, i).x;
    Y[i.z*XY+i.y*X+i.x] = alpha * a + beta * b;
}

kernel void estimate(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t B, read_only image2d_t B0, read_only image2d_t B1, read_only image3d_t AX) { // y = (b / (b0·exp(-Ax)+b1) - 1) b0·exp(-Ax) [OSTR]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float b = read_imagef(B, sampler, i).x;
    float b0 = 1; //read_imagef(B0, sampler, i.xy).x;
    float b1 = 1; //read_imagef(B1, sampler, i.xy).x;
    float Ax = read_imagef(AX, sampler, i).x;
    float d = b1*exp(-Ax) + b0; // >b0
    Y[i.z*XY+i.y*X+i.x] = ((b / d) - 1) * b1*exp(-Ax);
}

kernel void curvature(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t B, read_only image2d_t B0, read_only image2d_t B1, read_only image3d_t AX, read_only image3d_t AI, read_only image3d_t H) { // [OSTR]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float b = read_imagef(B, sampler, i).x;
    float b0 = 1; //read_imagef(B0, sampler, i.xy).x;
    float b1 = 1; //read_imagef(B1, sampler, i.xy).x;
    float Ax = read_imagef(AX, sampler, i).x;
    float Ai = read_imagef(AI, sampler, i).x;
    float h = read_imagef(H, sampler, i).x;
    float h0 = (b / (b1 + b0) - 1 ) * b1; // FIXME: precompute
    Y[i.z*XY+i.y*X+i.x] = - Ai * max(0.f, Ax > 0 ?  2.f*( h0 - h + Ax*h ) / (Ax*Ax) : h0); // - Ai c
}

kernel void maximize(global float* Y, const uint XY, const uint X, sampler_t sampler, read_only image3d_t A, read_only image3d_t L, read_only image3d_t D) { // [OSTR]
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float x = read_imagef(A, sampler, i).x;
    float l = read_imagef(L, sampler, i).x;
    float d = read_imagef(D, sampler, i).x;
    float y = x;
    y = max(0.f, d < 0 ? y + ( l - d*(y-x) ) / d : y); // Newton-Raphson
    //y = max(0.f, d < 0 ? y + ( l - d*(y-x) ) / d : y); // Newton-Raphson
    Y[i.z*XY+i.y*X+i.x] = y;
}
