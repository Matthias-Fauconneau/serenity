terrain {
 varying vec2 vTexCoords;
 uniform int W;
 vertex {
  uniform mat4 modelViewProjectionTransform; // TODO: -> mat3
  int yx = gl_VertexID;
  int y = yx/W, x = yx%W;
  uniform vec2 originAngles;
  uniform float angularResolution;
  vec2 angles = originAngles + angularResolution * vec2(x, y);
  uniform float R;
  float r = 1+aElevation/R;
  attribute float aElevation;
  gl_Position = modelViewProjectionTransform*vec4(r*sin(angles.y)*cos(angles.x), r*sin(angles.y)*sin(angles.x), r*cos(angles.y), 1);
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
  color.rgb = vec3(float(texelFetch(tElevation, int(vTexCoords.y)*W+int(vTexCoords.x)).r)/4096);
 }
}
