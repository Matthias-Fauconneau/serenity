//varying vec3 vertexNormal;

vertex {
	attribute vec4 position;
	uniform mat4 view;
	gl_Position = view * position;

	//attribute vec3 normal;
	//vertexNormal = normal;
}

fragment {
	uniform vec4 color;
	gl_FragColor = color; //dot(vertexNormal,vec3(0,0,1)) * color;
}
