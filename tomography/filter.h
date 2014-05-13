#pragma once
#include <fftw3.h> //fftw3f

struct FFTW : handle<fftwf_plan> { using handle<fftwf_plan>::handle; default_move(FFTW); FFTW(){} ~FFTW(); };
inline FFTW::~FFTW() { if(pointer) fftwf_destroy_plan(pointer); }

struct Filter {
    buffer<float> r, hc;
    FFTW forward, backward;
    Filter(){}
    Filter(uint N) : r(N), hc(N), forward(fftwf_plan_r2r_1d(N, r, hc, FFTW_R2HC, FFTW_ESTIMATE)), backward(fftwf_plan_r2r_1d(N, hc, r, FFTW_HC2R, FFTW_ESTIMATE)) {}
    operator mref<float>() { return r.slice(r.size/4,r.size/2); }
    ref<float> filter(ref<float> row) {
        assert_(row.size*2 == r.size);
        uint W = row.size;
        for(uint i: range(W)) r[W/2+i] = row[i];
        return filter();
    }
    ref<float> filter() {
        int start = r.size/4; float firstR = r[start];  for(uint i: range(start)) r[i] = firstR;
        int end = r.size/4+r.size/2; float lastR = r[end-1]; for(uint i: range(end,r.size)) r[i] = lastR;
        fftwf_execute(forward);
        uint N = r.size;
        float scale = 16./(N*N);
        for(uint i: range(N/2+1)) hc[i] *= scale * float(i); // Multiplies real parts by |w| ( / N normalized later )
        for(uint i: range(1,N/2)) hc[N-i] *= scale * float(i); // Multiplies imaginary parts by |w| ( / N normalized later )
        fftwf_execute(backward);
        return r.slice(N/4,N/2);
    }
};
