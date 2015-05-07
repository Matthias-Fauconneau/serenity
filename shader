vertex {
        in vec3 position;
        gl_Position = vec4(position, 1);
        out vec2 vTexCoords;
        vTexCoords = vec2[](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(-1,1),vec2(1,-1),vec2(1,1))[gl_VertexID%6];
}
fragment {
        out vec4 color;
        in vec2 vTexCoords;
        sphere {
                if(length(vTexCoords) > 1) discard;
                color = vec4(vec3(1-length(vTexCoords)), 1);
        }
        cylinder {
                color = vec4(vec3(1-abs(vTexCoords.x)), 1);
        }
}
