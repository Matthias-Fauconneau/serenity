terrain {
 varying vec2 vTexCoords;
 uniform int W;
 uniform float sphericalResolution;
 vertex {
  int yx = gl_VertexID;
  int y = yx/W, x = yx%W;
  uniform vec2 sphericalOrigin;
  vec2 s = sphericalOrigin + sphericalResolution * vec2(x, y); // Spherical position
  uniform float R;
  float r = 1+aElevation/R;
  attribute float aElevation;
  uniform mat4 modelViewProjectionTransform;
  gl_Position = modelViewProjectionTransform*vec4(r*sin(s.y)*cos(s.x), r*sin(s.y)*sin(s.x), r*cos(s.y), 1);
  vTexCoords = vec2(x, y);
 }
}

fragment {
 out vec4 color;
 color = vec4(1);
}

terrain {
 fragment {
  uniform samplerBuffer tElevation;
  float resolution = 3.14/32/675/sphericalResolution;
  color.rgb = vec3(resolution, vec2(float(texelFetch(tElevation, int(vTexCoords.y)*W+int(vTexCoords.x)).r)/4096));
 }
}
