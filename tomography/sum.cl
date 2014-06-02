__kernel void SSQ(__global float*  A, __global float* output, size_t count, __local volatile float* scratch) {
    // Performs first level block-parallel sequential reduction from global memory to scratch memory
    size_t tid = get_local_id(0);
    size_t i = get_group_id(0)*get_local_size(0) + get_local_id(0);
    size_t gridSize = blockSize*get_num_groups(0);
    sdata[tid] = 0;
    while (i < count) { sdata[tid] += A[i]*A[i]; i += gridSize; }
    barrier(CLK_LOCAL_MEM_FENCE);
    // Reduces each block
    if(blockSize >= 512) { if (tid < 256) { sdata[tid] += sdata[tid + 256]; } barrier(CLK_LOCAL_MEM_FENCE); } // 512->256
    if(blockSize >= 256) { if (tid < 128) { sdata[tid] += sdata[tid + 128]; } barrier(CLK_LOCAL_MEM_FENCE); } // 256->128
    if(blockSize >= 128) { if (tid <   64) { sdata[tid] += sdata[tid +   64]; } barrier(CLK_LOCAL_MEM_FENCE); } // 128->64
    if(tid < 32) { // No need for barriers anymore on warp-sized operations (Unnecessary sums will be scheduled)
        if (blockSize >=  64) { sdata[tid] += sdata[tid + 32]; } //64->32
        if (blockSize >=  32) { sdata[tid] += sdata[tid + 16]; } //32->16
        if (blockSize >=  16) { sdata[tid] += sdata[tid +   8]; } //16->8
        if (blockSize >=    8) { sdata[tid] += sdata[tid +   4]; } //  8-> 4
        if (blockSize >=    4) { sdata[tid] += sdata[tid +   2]; } //  4-> 2
        if (blockSize >=    2) { sdata[tid] += sdata[tid +   1]; } //  2-> 1
    }
    if(tid == 0) g_odata[get_group_id(0)] = sdata[0]; // Outputs block sum back to global memory
}

__kernel void SSE(__global float*  A, __global float*  B, __global float* output, size_t count, __local volatile float* scratch) {
    // Performs first level block-parallel sequential reduction from global memory to scratch memory
    size_t tid = get_local_id(0);
    size_t i = get_group_id(0)*get_local_size(0) + get_local_id(0);
    size_t gridSize = blockSize*get_num_groups(0);
    sdata[tid] = 0;
    while (i < count) { sdata[tid] += A[i]*B[i]; i += gridSize; }
    barrier(CLK_LOCAL_MEM_FENCE);
    // Reduces each block
    if(blockSize >= 512) { if (tid < 256) { sdata[tid] += sdata[tid + 256]; } barrier(CLK_LOCAL_MEM_FENCE); } // 512->256
    if(blockSize >= 256) { if (tid < 128) { sdata[tid] += sdata[tid + 128]; } barrier(CLK_LOCAL_MEM_FENCE); } // 256->128
    if(blockSize >= 128) { if (tid <   64) { sdata[tid] += sdata[tid +   64]; } barrier(CLK_LOCAL_MEM_FENCE); } // 128->64
    if(tid < 32) { // No need for barriers anymore on warp-sized operations (Unnecessary sums will be scheduled)
        if (blockSize >=  64) { sdata[tid] += sdata[tid + 32]; } //64->32
        if (blockSize >=  32) { sdata[tid] += sdata[tid + 16]; } //32->16
        if (blockSize >=  16) { sdata[tid] += sdata[tid +   8]; } //16->8
        if (blockSize >=    8) { sdata[tid] += sdata[tid +   4]; } //  8-> 4
        if (blockSize >=    4) { sdata[tid] += sdata[tid +   2]; } //  4-> 2
        if (blockSize >=    2) { sdata[tid] += sdata[tid +   1]; } //  2-> 1
    }
    if(tid == 0) g_odata[get_group_id(0)] = sdata[0]; // Outputs block sum back to global memory
}
