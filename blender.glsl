transform {
 vertex {
  uniform mat4 modelViewProjectionTransform;
  attribute vec3 aPosition;
  vec3 position = aPosition;
  gl_Position = modelViewProjectionTransform*vec4(position,1);
 }
}

instancedTransform {
 vertex {
  uniform mat4 viewProjectionTransform;
  attribute vec4 aModelTransform0, aModelTransform1, aModelTransform2, aModelTransform3;
  mat4 aModelTransform;
  aModelTransform[0] = aModelTransform0;
  aModelTransform[1] = aModelTransform1;
  aModelTransform[2] = aModelTransform2;
  aModelTransform[3] = aModelTransform3;
  attribute vec3 aPosition;
  vec3 position = (aModelTransform * vec4(aPosition,1)).xyz;
  gl_Position = viewProjectionTransform*vec4(position,1);
 }
}

normal {
 varying vec3 vNormal;
 vertex {
  uniform mat3 normalMatrix;
  attribute vec3 aNormal;
  vNormal = normalMatrix*aNormal;
 }
 fragment {
  vec3 normal = normalize(vNormal);
 }
}

instancedNormal {
 varying vec3 vNormal;
 vertex {
  attribute vec3 aNormalMatrix0, aNormalMatrix1, aNormalMatrix2;
  mat3 aNormalMatrix;
  aNormalMatrix[0] = aNormalMatrix0;
  aNormalMatrix[1] = aNormalMatrix1;
  aNormalMatrix[2] = aNormalMatrix2;
  attribute vec3 aNormal;
  vNormal = aNormalMatrix*aNormal;
 }
 fragment {
  vec3 normal = normalize(vNormal);
 }
}

texCoord {
 varying vec2 vTexCoord;
 vertex {
  attribute vec2 aTexCoord;
  vTexCoord = aTexCoord;
 }
}

diffuse {
 fragment {
  vec3 diffuseLight = vec3(0,0,0);
 }
}

shadow {
 uniform float shadowScale;
 uniform sampler2DShadow shadowMap;
 varying vec4 shadowPosition;
 vertex {
  uniform mat4 shadowTransform;
  shadowPosition = shadowTransform*vec4(position,1);
 }
 fragment {
  float PCF=0.0;
  for(float i=-0.5; i<=0.5; i++) for(float j=-0.5; j<=0.5; j++) //2x2 PCF + 2x2 HW PCF
   PCF += shadow2DProj(shadowMap, vec4((shadowPosition.xy+vec2(i,j)*shadowScale)*shadowPosition.w,shadowPosition.zw)).r;
  float shadowLight = 1.0-PCF/4.0;
 }
}

sun {
 fragment {
  //const vec3 sunColor = vec3(0.75, 0.5, 0.25);
  const vec3 sunColor = vec3(0.875, 0.75, 0.5);
  uniform vec3 sunLightDirection;
  diffuseLight += shadowLight * max(0,dot(sunLightDirection, normal)) * sunColor;
 }
}

sky {
 fragment {
  //const vec3 skyColor = vec3(0.25, 0.5, 0.75);
  const vec3 skyColor = vec3(0.125, 0.25, 0.5);
  uniform vec3 skyLightDirection;
  diffuseLight += (1.f+dot(skyLightDirection, normal))/2.f * skyColor;
 }
}

screen {
 varying vec2 texCoord;
 vertex {
  attribute vec2 position;
  gl_Position = vec4(position,0,1);
  texCoord = (position+1.0)/2.0;
 }
}

skymap {
 varying vec3 viewRay;
 vertex {
  attribute vec2 position;
  gl_Position = vec4(position,0.999,1);
  uniform mat4 inverseViewProjectionMatrix;
  vec4 viewPos = (inverseViewProjectionMatrix * vec4(position.xy,1,1));
  viewRay = viewPos.xyz/viewPos.w;
 }
 fragment {
  uniform sampler2D Skymap_offworld_gen2;
  const float PI = 3.14159265358979323846;
  vec2 equirectangular = vec2(atan(viewRay.y,viewRay.x)/(2.0*PI), acos(normalize(viewRay).z)/PI);
  equirectangular.x = 0.5-equirectangular.x; // flip x
  gl_FragColor.rgb = texture2D(Skymap_offworld_gen2, equirectangular).rgb;
 }
}

resolve {
 fragment {
  uniform sampler2D framebuffer;
  float sRGB(float c) { if(c>=0.0031308) return 1.055*pow(c,1.0/2.4)-0.055; else return 12.92*c; }
  vec3 c = texture2D(framebuffer, texCoord).rgb;
  gl_FragColor = vec4(sRGB(c.r), sRGB(c.g), sRGB(c.b), 1);
 }
}
