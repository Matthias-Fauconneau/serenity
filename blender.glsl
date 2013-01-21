transform {
 vertex {
  uniform mat4 modelViewProjectionTransform;
  attribute vec3 position;
  gl_Position = modelViewProjectionTransform*vec4(position,1);
 }
}

fragment {
 gl_FragColor = vec4(0,0,0,1);
}

normal {
 varying vec3 _normal;
 vertex {
  uniform mat3 normalMatrix;
  attribute vec3 normal;
  _normal = normalMatrix*normal;
 }
 fragment {
  vec3 normal = normalize(_normal);
 }
}

diffuse {
 varying vec3 diffuseColor;
 vertex {
  attribute vec3 color;
  diffuseColor = color;
 }
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
  const vec3 sunColor = vec3(0.875, 0.75, 0.5); //(0.75, 0.5, 0.25);
  uniform vec3 sunLightDirection;
  diffuseLight += shadowLight * max(0,dot(sunLightDirection, normal)) * sunColor;
 }
}

sky {
 fragment {
  const vec3 skyColor = vec3(0.125, 0.25, 0.5); //(0.25, 0.5, 0.75)
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

diffuse {
 fragment {
  gl_FragColor.rgb += diffuseColor*diffuseLight;
 }
}

atmosphere {
 varying vec3 viewRay;
 vertex {
  attribute vec2 position;
  gl_Position = vec4(position,0.999,1);
  uniform mat4 inverseProjectionMatrix;
  vec4 viewPos = (inverseProjectionMatrix * vec4(position.xy,1,1));
  viewRay = viewPos.xyz/viewPos.w;
 }
 fragment {
  uniform vec3 sunLightDirection;
  const float Kr=0.0025, Km=0.0001, g=-0.990;
  float cos = dot(sunLightDirection, normalize(viewRay));
  float miePhase = ((1.0 - g*g) / (2.0 + g*g)) * (1.0 + cos*cos) / pow(1.0 + g*g - 2.0*g*cos, 1.5);
  const vec3 skyColor = vec3(0.125, 0.25, 0.5); //(0.25, 0.5, 0.75)
  const vec3 sunColor = vec3(0.875, 0.75, 0.5); //(0.75, 0.5, 0.25);
  gl_FragColor.rgb = skyColor + sunColor*20.0*Km*miePhase;
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
