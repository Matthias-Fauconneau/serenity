vertex {
        in vec3 position;
        gl_Position = vec4(position, 1);
        sphere {
         out vec2 vTexCoords;
         vTexCoords = vec2[](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(-1,1),vec2(1,-1),vec2(1,1))[gl_VertexID%6];
        }
        cylinder {
         out vec2 vTexCoords;
         vTexCoords = vec2[](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(-1,1),vec2(1,-1),vec2(1,1))[gl_VertexID%6];
        }
        flat {
         in vec3 aColor;
         out vec3 vColor;
         vColor = aColor;
        }
}
fragment {
        out vec4 color;
        flat {
          in vec3 vColor;
          color = vec4(vColor, 1);
        }
        sphere {
                in vec2 vTexCoords;
                if(length(vTexCoords) > 1) discard;
                float dz = sqrt(1-dot(vTexCoords,vTexCoords));
                color = vec4(vec3(dz), 1);
                gl_FragDepth = gl_FragCoord.z - dz/4/2/4;
        }
        cylinder {
                in vec2 vTexCoords;
                float dz = 1-abs(vTexCoords.x);
                color = vec4(vec3(dz), 1);
                gl_FragDepth = gl_FragCoord.z - dz/4/2/4;
        }
}
