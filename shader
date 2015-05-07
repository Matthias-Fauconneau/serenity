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
                float dz = sqrt(1-dot(vTexCoords,vTexCoords));

        }
        cylinder {
                float dz = 1-abs(vTexCoords.x);
        }
        color = vec4(vec3(dz), 1);
        gl_FragDepth = gl_FragCoord.z - dz/4/2/4;
}
