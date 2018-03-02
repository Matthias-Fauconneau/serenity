#include "vector.h"
#include "math.h"
#include "mwc.h"
#include "simd.h"
#include "ray.h"

static inline float rcp(float x) { return 1/x; }
static inline float pow5(float x) { const float x2=x*x; return x2*x2*x; }

static inline float normalDensityGGX(const float a, const vec3 N, const vec3 M) {
    const float a2 = sq(a);
    const float NH = ::max(0.f, dot(N, M));
    return a2 * rcp(π * sq(sq(NH)*(a2-1)+1));
}

static inline Vec<v8sf, 3> normalDistributionGGX(const float a, const v8sf ξ1, const v8sf ξ2) {
    const v8sf cos²θ = (1-ξ1)/((sq(a)-1)*ξ1+1);
    const v8sf cosθ = sqrt(cos²θ);
    const v8sf sinθ = sqrt(1-cos²θ);
    const v8sf φ = 2*π*ξ2;
    const Vec<v8sf, 2> cossinφ = cossin(φ);
    return {{sinθ * cossinφ._[0], sinθ * cossinφ._[1], cosθ}};
}
static inline Vec<v8sf, 3> normalDistributionGGX(const float a, Random& random) { return normalDistributionGGX(a, random(),random()); }

vec3 normalDistributionGGX(Random& random, const float a, const vec3 T, const vec3 B, const vec3 N) {
    const Vec<v8sf, 3> D8 = ::normalDistributionGGX(a, random); // Random ray direction drawn from cosine probability distribution
    return transform(T,B,N, vec3(D8._[0][0], D8._[1][0], D8._[2][0]));
}

static inline float reflectanceGGX_D(const float a, const vec3 N, const vec3 L, const vec3 V) {
    const float a2 = sq(a);
    const vec3 M = normalize(L+V);
    const float VM = ::max(0.f, dot(V, M));
    //const float n1 = 1, n2 = 2.63f;
    const float F0 = 1; //sq((n1-n2)/(n1+n2));
    const float F = F0 + (1-F0)*pow5(1-VM);
    const float NV = ::max(0.f, dot(N, V));
    const float NL = ::max(0.f, dot(N, L));
    const float _2NV_GV = NV + sqrt( (NV - NV * a2) * NV + a2 );
    const float _2NL_GL = NL + sqrt( (NL - NL * a2) * NL + a2 );
    const float G_4NLNV = rcp(_2NV_GV * _2NL_GL);
    return F * G_4NLNV;
}

static inline float reflectanceGGX(const float a, const vec3 N, const vec3 L, const vec3 V) {
    const float a2 = sq(a);
    const vec3 M = normalize(L+V);
    const float NM = ::max(0.f, dot(N, M));
    const float D = a2 * rcp(π * sq(sq(NM)*(a2-1)+1));
    return D * reflectanceGGX_D(a, N, L, V);
}
