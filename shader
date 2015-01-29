terrain {
 varying vec2 vTexCoords;
 uniform int N;
 vertex {
  uniform mat4 modelViewProjectionTransform; // TODO: -> mat3
  vTexCoords = vec2(gl_VertexID%(N+1), gl_VertexID/(N+1));
  attribute float aElevation;
  gl_Position = modelViewProjectionTransform*vec4(vTexCoords, aElevation, 1);
 }
}

transform {
 vertex {
  uniform mat4 modelViewProjectionTransform;
  attribute vec3 aPosition;
  gl_Position = modelViewProjectionTransform*vec4(aPosition,1);
 }
}

normal {
 varying vec3 vNormal;
 vertex {
  attribute vec3 aNormal;
  vNormal = aNormal;
 }
 fragment { vec3 normal = normalize(vNormal); }
}

screen {
 varying vec2 vTexCoords;
 vertex {
  attribute vec2 position;
  gl_Position = vec4(position,0,1);
  vTexCoords = (position+1)/2;
 }
}

fragment {
 out vec4 color;
 color = vec4(1);
 diffuse {
   vec3 diffuseLight = vec3(0.3,0.3,0.4); // Ambient light
 }
}

terrain {
 fragment {
  uniform isamplerBuffer tElevation;
  color.rgb = vec3(float(texelFetch(tElevation, int(vTexCoords.y)*(N+1)+int(vTexCoords.x)).r)/256);
 }
}

color {
 varying vec3 vColor;
 vertex {
  attribute vec3 aColor;
  vColor = aColor;
 }
 fragment { color.rgb = vColor; }
}

light {
 fragment {
  float light = 1;
 }
}

shadow {
 varying vec4 shadowPosition;
 vertex {
  uniform mat4 shadowTransform;
  shadowPosition = shadowTransform * vec4(aPosition,1);
 }
 fragment {
  uniform sampler2DShadow shadow;
  light = textureProj(shadow, vec4(shadowPosition.xy, shadowPosition.z - 1./256 /*FIXME*/, shadowPosition.w));
 }
}

light {
 fragment {
  const vec3 lightColor = vec3(1, 1, 1);
  uniform vec3 lightDirection;
  diffuseLight += light * max(0,dot(lightDirection, normal)) * lightColor;
 }
}

diffuse { fragment { color.rgb *= diffuseLight; } }

sky {
 varying vec3 vTexCoords;
 vertex {
  uniform mat4 inverseViewProjectionMatrix;
  vec2 aPosition;
  if(gl_VertexID==0) aPosition=vec2(-1,-1);
  if(gl_VertexID==1) aPosition=vec2( 1,-1);
  if(gl_VertexID==2) aPosition=vec2(-1, 1);
  if(gl_VertexID==3) aPosition=vec2( 1, 1);
  gl_Position = vec4(aPosition, 0.999999, 1);
  vec4 viewPos = inverseViewProjectionMatrix * vec4(aPosition.xy, 1, 1);
  vTexCoords = viewPos.xyz; //viewPos.w;
 }
 fragment {
  color.rgb = vec3(1-abs(vTexCoords.z)/2, 1-abs(vTexCoords.z)/3, 1);
 }
}

present {
 fragment {
  uniform sampler2D framebuffer;
  float sRGB(float c) { if(c>=0.0031308) return 1.055*pow(c,1.0/2.4)-0.055; else return 12.92*c; }
  vec3 c = texture2D(framebuffer, vTexCoords).rgb;
  color = vec4(sRGB(c.r), sRGB(c.g), sRGB(c.b), 1);
 }
}
