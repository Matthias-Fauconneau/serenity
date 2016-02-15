vertex {
  uniform mat4 transform;
  packed {
   in float x, y, z;
   gl_Position = transform * vec4(x, y, z, 1);
  }
  interleaved {
   in vec3 position;
   gl_Position = transform * vec4(position, 1);
  }
  out vec2 vLocalCoords;
  uniform float hpxRadius;
  float x = 1+hpxRadius;
  vLocalCoords = vec2[](vec2(-x,-x),vec2(x,-x),vec2(-x,x),
      vec2(-x,x),vec2(x,-x),vec2(x,x))[gl_VertexID%6];
}
fragment {
  out vec4 color;
  in vec2 vLocalCoords;
  sphere {
    uniform float hpxRadius;
    float l = length(vLocalCoords);
    if(l > 1+hpxRadius) discard;
    float dz = sqrt(max(0, 1-dot(vLocalCoords,vLocalCoords)));
    float a = ((1+hpxRadius)-l)/(2*hpxRadius);
    color = vec4(vec3(dz), a);
    buffer rotationBuffer { vec4[] rotation; };
    vec4 q = rotation[gl_PrimitiveID/2];
    vec4 conjugate(vec4 q) { return vec4(-q.xyz, q.w); }
    vec4 qmul(vec4 p, vec4 q) {
      return vec4(p.w*q.xyz + q.w*p.xyz + cross(p.xyz, q.xyz),
                  p.w*q.w - dot(p.xyz, q.xyz));
    }
    vec3 mul(vec4 p, vec3 v) {
      return qmul(p, qmul(vec4(v, 0), vec4(-p.xyz, p.w))).xyz;
    }
    uniform vec4 viewRotation;
    vec3 v = mul(conjugate(qmul(viewRotation, q)), vec3(vLocalCoords, dz));
    mat2x3 D = mat2x3(dFdx(v), dFdy(v));
    mat3x2 Dt = transpose(D);
    vec3 G = vec3(length(Dt[0]), length(Dt[1]), length(Dt[2]));
    vec3 S = vec3(
                clamp((v.x-G.x/2)/G.x, 0, 1),
                clamp((v.y-G.y/2)/G.y, 0, 1),
                clamp((v.z-G.z/2)/G.z, 0, 1));
    //buffer colorBuffer { vec4[] aColor; };
    color = /*aColor[gl_PrimitiveID/2]**/vec4(dz*S, a);
    uniform float radius;
    gl_FragDepth = gl_FragCoord.z + dz * radius;
  }
  cylinder {
    float l = abs(vLocalCoords.x);
    float dz = sqrt(max(0,1-l));
    uniform float hpxRadius;
    float a = ((1+hpxRadius)-l)/(2*hpxRadius);
    //buffer colorBuffer { vec4[] aColor; };
    color = vec4(dz*(gl_PrimitiveID/2%2==0?vec3(2./4):vec3(3./4)), a);// * aColor[gl_PrimitiveID/2];
    uniform float radius;
    gl_FragDepth = gl_FragCoord.z + dz * radius;
  }
  blit {
    uniform sampler2D image;
    color = vec4(texture2D(image, (vLocalCoords+1)/2).rgb, 1);
  }
  flat {
    uniform vec4 uColor;
    color = uColor;
  }
}

