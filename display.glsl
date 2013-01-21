vertex {
 attribute vec2 position;
 gl_Position = vec4(position,0,1);
}

fragment {
 uniform vec4 color;
 gl_FragColor = color;
}

blit {
 attribute vec2 texCoord;
 varying vec2 _texCoord;
 vertex {
  _texCoord = texCoord;
 }
 fragment {
  uniform sampler2D sampler;
  gl_FragColor = color * texture2D(sampler, _texCoord);
 }
}
