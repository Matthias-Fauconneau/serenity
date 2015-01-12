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

color {
 varying vec3 vColor;
 vertex {
  attribute vec3 aColor;
  vColor = aColor;
 }
 fragment { color.rgb = vColor; }
}

light {
 varying vec4 shadowPosition;
 vertex {
  uniform mat4 shadowTransform;
  shadowPosition = shadowTransform * vec4(aPosition,1);
 }
 fragment {
  uniform sampler2DShadow shadow;
  float shadowLight = textureProj(shadow, vec4(shadowPosition.xy, shadowPosition.z -  1./128, shadowPosition.w));
  const vec3 lightColor = vec3(1, 1, 1);
  uniform vec3 lightDirection;
  diffuseLight += shadowLight * max(0,dot(lightDirection, normal)) * lightColor;
 }
}

diffuse { fragment { color.rgb *= diffuseLight; } }

present {
 fragment {
  uniform sampler2D framebuffer;
  float sRGB(float c) { if(c>=0.0031308) return 1.055*pow(c,1.0/2.4)-0.055; else return 12.92*c; }
  vec3 c = texture2D(framebuffer, vTexCoords).rgb;
  color = vec4(sRGB(c.r), sRGB(c.g), sRGB(c.b), 1);
 }
}
