terrain {
 varying vec2 vTexCoords;
 uniform int W;
 vertex {
  uniform mat4 modelViewProjectionTransform; // TODO: -> mat3
  /*int YXyx = gl_VertexID;
  int x = YXyx%(N+1);
  int YXy = YXyx/(N+1);
  int y = YXy%(N+1);
  int YX = YXy/(N+1);*/
  int YX = gl_VertexID;
  int X = YX%W;
  int Y = YX/W;
  uniform float dx, dy;
  float longitude = /*(X*N+x)*/ X*dx;
  float PI = 3.14159265358979323846;
  float latitude = /*(Y*N+y)*/ /*PI/6 +*/ Y*dy;
  uniform float R;
  float r = 1+aElevation/R;
  attribute float aElevation;
  gl_Position = modelViewProjectionTransform*vec4(r*sin(latitude)*cos(longitude), r*sin(latitude)*sin(longitude), r*cos(latitude), 1);
  vTexCoords = vec2(X, Y); //vec2(x, YXy);
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
