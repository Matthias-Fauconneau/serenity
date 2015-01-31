terrain {
 varying vec2 vTexCoords;
 uniform int N;
 vertex {
  uniform mat4 modelViewProjectionTransform; // TODO: -> mat3
  /*int YXyx = gl_VertexID;
  int x = YXyx%(N+1);
  int YXy = YXyx/(N+1);
  int y = YXy%(N+1);
  int YX = YXy/(N+1);*/
  int YX = gl_VertexID;
  uniform int W;
  int X = YX%W;
  int Y = YX/W;
  uniform float da;
  float longitude = /*(X*N+x)*/ X*da;
  float latitude = /*(Y*N+y)*/ Y*da;
  /*uniform*/ float R = 1;
  attribute float aElevation;
  gl_Position = modelViewProjectionTransform*vec4(R*sin(longitude), R*sin(latitude), (R+aElevation)*cos(longitude)*cos(latitude), 1);
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
  color.rgb = vec3(0/*float(texelFetch(tElevation, int(vTexCoords.y)*W+int(vTexCoords.x)).r)/1024*/);
 }
}
