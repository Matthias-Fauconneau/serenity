// y := alpha * a + beta * b;
kernel void apply(image3d_t Y, const float alpha, read_only image3d_t A, const float beta, read_only image3d_t B) {
    size_t x = get_global_id(0); size_t y = get_global_id(1); size_t z = get_global_id(2);
    write_imagef(Y, (int4)(x,y,z,0), alpha * read_imagef(A, (int4)(x,y,z,0)).x + beta * read_imagef(B, (int4)(x,y,z,0)).x);
}
