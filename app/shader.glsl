#extension GL_ARB_bindless_texture : enable
#extension GL_NV_gpu_shader5 : enable
vertex {
 in float X;
 in float Y;
 in float Z;
 uniform mat4 view1;
 gl_Position = view1 * vec4(X, Y, Z, 1);
 uv {
  out vec2 uv;
  //const vec2 UV[6] = vec2[](vec2(0,0),vec2(1,0),vec2(0,1), vec2(0,1),vec2(1,0),vec2(1,1));
  const vec2 UV[6] = vec2[](vec2(0,0),vec2(1,0),vec2(1,1), vec2(0,0),vec2(1,1),vec2(0,1));
  uv = UV[gl_VertexID%6];
 }
 ptex {
  out vec2 uv;
  //const vec2 UV[6] = vec2[](vec2(0,0),vec2(1,0),vec2(0,1), vec2(0,1),vec2(1,0),vec2(1,1));
  const vec2 UV[6] = vec2[](vec2(0,0),vec2(1,0),vec2(1,1), vec2(0,0),vec2(1,1),vec2(0,1));
  uv = UV[gl_VertexID%6];
  out vec3 O0;
  O0 = vec3(X, Y, Z);
 }
}
fragment {
 out vec4 pixel;
 pixel.a = 1;
 uv {
  in vec2 uv;
  pixel = vec4(uv, 0, 1);
 }
 ptex {
  const int quadIndex = gl_PrimitiveID/2;
  in vec2 uv;
  uniform vec2 st;
  buffer radiosityTextureHandleBuffer { uint64_t radiosityTextureHandles[]; };
  uint64_t radiosityTextureHandle = radiosityTextureHandles[quadIndex];
  buffer nXwBuffer { int nXw[]; };
  buffer nYwBuffer { int nYw[]; };
  ivec2 size = ivec2(nXw[quadIndex], nYw[quadIndex]);
  if(size.x == 1 && size.y == 1) {
      pixel.rgb = texture(sampler2DArray(radiosityTextureHandle), vec3(uv,0)).rgb;
  } else {
   vec2 x = st*size;
   ivec2 i = ivec2(x);
   nearest {
    pixel.rgb = texture(sampler2DArray(radiosityTextureHandle), vec3(uv,i.y*size.x+i.x)).rgb;
   }
   linear {
    vec2 f = x-i;
    vec3 v00 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, i.y*size.x+i.x)).rgb;
    vec3 v01 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, i.y*size.x+min(i.x+1,size.x-1))).rgb;
    vec3 v10 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, min(i.y+1,size.y-1)*size.x+i.x)).rgb;
    vec3 v11 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, min(i.y+1,size.y-1)*size.x+min(i.x+1,size.x-1))).rgb;
    pixel.rgb = mix(mix(v00, v01, f.x), mix(v10, v11, f.x), f.y);
   }
   cubic {
    vec2 f = x-i;
    vec3 v00 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, max(i.y-1,0)*size.x+max(0,i.x-1       ))).rgb;
    vec3 v01 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, max(i.y-1,0)*size.x+      i.x          )).rgb;
    vec3 v02 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, max(i.y-1,0)*size.x+min(i.x+1,size.x-1))).rgb;
    vec3 v03 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, max(i.y-1,0)*size.x+min(i.x+2,size.x-1))).rgb;

    vec3 v10 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, i.y*size.x+max(0,i.x-1       ))).rgb;
    vec3 v11 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, i.y*size.x+      i.x          )).rgb;
    vec3 v12 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, i.y*size.x+min(i.x+1,size.x-1))).rgb;
    vec3 v13 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, i.y*size.x+min(i.x+2,size.x-1))).rgb;

    vec3 v20 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, min(i.y+1,size.y-1)*size.x+max(0,i.x-1       ))).rgb;
    vec3 v21 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, min(i.y+1,size.y-1)*size.x+      i.x          )).rgb;
    vec3 v22 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, min(i.y+1,size.y-1)*size.x+min(i.x+1,size.x-1))).rgb;
    vec3 v23 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, min(i.y+1,size.y-1)*size.x+min(i.x+2,size.x-1))).rgb;

    vec3 v30 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, min(i.y+2,size.y-1)*size.x+max(0,i.x-1         ))).rgb;
    vec3 v31 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, min(i.y+2,size.y-1)*size.x+      i.x            )).rgb;
    vec3 v32 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, min(i.y+2,size.y-1)*size.x+min(  i.x+1,size.x-1))).rgb;
    vec3 v33 = texture(sampler2DArray(radiosityTextureHandle), vec3(uv, min(i.y+2,size.y-1)*size.x+min(  i.x+2,size.x-1))).rgb;
    float sq(float x) { return x*x; }
    float cb(float x) { return x*x*x; }
    float wx0 = cb(1-f.x)/6, wx1 = 2./3 - sq(f.x)*(2-f.x)/2, wx2 = 2./3 - sq(1-f.x)*(2-(1-f.x))/2, wx3 = cb(f.x)/6;
    float wy0 = cb(1-f.y)/6, wy1 = 2./3 - sq(f.y)*(2-f.y)/2, wy2 = 2./3 - sq(1-f.y)*(2-(1-f.y))/2, wy3 = cb(f.y)/6;
    pixel.rgb =
            wy0*(wx0*v00 + wx1*v01 + wx2*v02 + wx3*v03) +
            wy1*(wx0*v10 + wx1*v11 + wx2*v12 + wx3*v13) +
            wy2*(wx0*v20 + wx1*v21 + wx2*v22 + wx3*v23) +
            wy3*(wx0*v30 + wx1*v31 + wx2*v32 + wx3*v33);
    bilateral {
        pixel.rgb =
                wy0*(wx0*v00 + wx1*v01 + wx2*v02 + wx3*v03) +
                wy1*(wx0*v10 + wx1*v11 + wx2*v12 + wx3*v13) +
                wy2*(wx0*v20 + wx1*v21 + wx2*v22 + wx3*v23) +
                wy3*(wx0*v30 + wx1*v31 + wx2*v32 + wx3*v33);

    }
   }
   raytrace {
    buffer NXBuffer { float NX[]; };
    buffer NYBuffer { float NY[]; };
    buffer NZBuffer { float NZ[]; };
    buffer X0Buffer { float X0[]; };
    buffer X1Buffer { float X1[]; };
    buffer X2Buffer { float X2[]; };
    buffer Y0Buffer { float Y0[]; };
    buffer Y1Buffer { float Y1[]; };
    buffer Y2Buffer { float Y2[]; };
    buffer Z0Buffer { float Z0[]; };
    buffer Z1Buffer { float Z1[]; };
    buffer Z2Buffer { float Z2[]; };
    vec3 N = vec3(NX[quadIndex], NY[quadIndex], NZ[quadIndex]);
    in vec3 O0;
    uniform vec3 viewOrigin;
    const uint n = 128;
    uniform float radicalInverse[n];
    for(int i=0;i<n;i++) {
     vec2 UV = vec2(float(i)/n, radicalInverse[i])+1/float(2*n); // 1/2n .. 1-1/2n
     vec3 O = O0 + (UV.x-1./2)*dFdx(O0) + (UV.y-1./2)*dFdy(O0);
     vec3 D = normalize(viewOrigin-O);
     vec3 R = normalize(2*dot(N, D)*N - D);

     const float inff = 1./0.;
     float minT = inff; float u = 0, v = 0; uint triangleIndex = -1;
     for(uint index=0; index<X0.length(); index++) {
         vec3 A = vec3(X0[index],Y0[index],Z0[index]);
         vec3 B = vec3(X1[index],Y1[index],Z1[index]);
         vec3 C = vec3(X2[index],Y2[index],Z2[index]);
         vec3 T = O - A;
         vec3 AB = B - A;
         vec3 AC = C - A;
         vec3 P = cross(R, AC);
         float U = dot(T, P);
         float det = dot(P, AB);
         vec3 Q = cross(T, AB);
         float V = dot(R, Q);
         float t = dot(AC, Q) / det;
         if(det > 0 && U >= 0 && V >= 0 && U + V <= det && t > 0.001 && t < minT) { // hmin(t), blend
             minT = t;
             u = U / det;
             v = V / det;
             triangleIndex = index;
         }
     }
     vec2 quad = (triangleIndex%2 == 0) ? vec2(u + v, v) : vec2(u, u + v);
     pixel.rgb += texture2D(sampler2D(radiosityTextureHandles[triangleIndex/2]), quad).rgb / n;
    }
   }
  }
  pixel.rgb = min(pixel.rgb, 1); // Clamps before resolve
 }
}
