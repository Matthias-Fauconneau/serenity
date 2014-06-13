kernel void add(image3d_t Y, const float alpha, read_only image3d_t A, const float beta, read_only image3d_t B) { // y = α a + β b
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, i).x;
    float b = read_imagef(B, i).x;
    write_imagef(Y, i, alpha * a + beta * b);
}

kernel void delta(image3d_t Y, read_only image3d_t A, read_only image3d_t B, read_only image3d_t C) { // y = c ? ( a - b ) / c : 0
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, i).x;
    float b = read_imagef(B, i).x;
    float c = read_imagef(C, i).x;
    write_imagef(Y, i, c ? ( a - b ) / c : 0);
}

kernel void update(image3d_t Y, read_only image3d_t A, const float alpha, read_only image3d_t B) { // y = max(0, a + b)
    int4 i = {get_global_id(0), get_global_id(1), get_global_id(2), 0};
    float a = read_imagef(A, i).x;
    float b = read_imagef(B, i).x;
    write_imagef(Y, i, max(0.f, a + alpha * b));
}
