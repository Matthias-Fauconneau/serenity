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
    /*buffer pRx { float[] Rx; };
    buffer pRy { float[] Ry; };
    buffer pRz { float[] Rz; };
    buffer pRw { float[] Rw; };
    //uniform vec4 viewRotation;
    vec4 q = vec4(Rx[gl_PrimitiveID/2], Ry[gl_PrimitiveID/2], Rz[gl_PrimitiveID/2], Rw[gl_PrimitiveID/2]);
    vec4 conjugate(vec4 q) { return vec4(-q.xyz, q.w); }
    vec4 qmul(vec4 p, vec4 q) {
      return vec4(p.w*q.xyz + q.w*p.xyz + cross(p.xyz, q.xyz),
                  p.w*q.w - dot(p.xyz, q.xyz));
    }
    vec3 mul(vec4 p, vec3 v) {
      return qmul(p, qmul(vec4(v, 0), vec4(-p.xyz, p.w))).xyz;
    }
    vec3 v = vec3(vLocalCoords, dz);
    color = vec4(dz*(1+mul(conjugate(qmul(viewRotation, q)), v))/2, a);*/
    color = vec4(vec3(dz), a);
    uniform float radius;
    gl_FragDepth = gl_FragCoord.z + dz * radius;
  }
  cylinder {
    float l = abs(vLocalCoords.x);
    float dz = sqrt(max(0,1-l));
    //color = vec4(dz*(gl_PrimitiveID/2%2==0?vec3(1,0,0):vec3(0,0,1)), 1);
    //buffer colorBuffer { vec4[] aColor; };
    uniform float hpxRadius;
    float a = ((1+hpxRadius)-l)/(2*hpxRadius);
    color = vec4(dz*(gl_PrimitiveID/2%2==0?vec3(2./4):vec3(3./4)), a) * aColor[gl_PrimitiveID/2];
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
