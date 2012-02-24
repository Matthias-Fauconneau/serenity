vertex {
	attribute vec2 position;
	uniform vec2 scale;
	uniform vec2 offset;
	gl_Position = vec4(scale * (position + offset) + vec2(-1,1), 0, 1);
	blit {
		attribute vec2 texCoord;
		varying vec2 UV;
		UV = texCoord;
	}
	radial {
		varying vec2 pos;
		pos = position;
	}
}
fragment {
	flat {
		uniform vec4 color;
		gl_FragColor = color;
	}
	radial {
		varying vec2 pos;
		uniform vec2 center;
		uniform float radius;
		uniform vec4 startColor, endColor;
		gl_FragColor = mix(startColor,endColor,min(1,length(pos-center)/radius));
	}
	blit {
		varying vec2 UV;
		uniform sampler2D image;
		gl_FragColor = texture2D(image, UV);
	}
}
