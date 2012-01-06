vertex {
	attribute vec2 position;
	uniform vec2 scale;
	uniform vec2 offset;
	gl_Position = vec4( scale * position + offset, 0, 1 );
	blit {
		attribute vec2 texCoord;
		varying vec2 UV;
		UV = texCoord;
	}
}
fragment {
	flat {
		uniform vec4 color;
		gl_FragColor = color;
	}
	blit {
		varying vec2 UV;
		uniform sampler2D image;
		gl_FragColor = texture2D(image, UV);
	}
}
