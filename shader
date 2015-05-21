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
                vec3 v = vec3(vTexCoords, dz);
                buffer rotationBuffer { vec4[] rotation; };
                vec4 q = rotation[gl_PrimitiveID/2].yzwx;
                vec4 qc = vec4(-q.x, -q.y, -q.z, q.w);
                vec4 qmul(vec4 p, vec4 q) {
                 return vec4(p.w*q.w - dot(p.xyz, q.xyz), p.w*q.xyz + q.w*p.xyz + cross(p.xyz, q.xyz));
                }
                color = vec4(vec3(dz)*(1+qmul(qmul(q, vec4(v, 1)), qc).xyz)/2, 1);
                gl_FragDepth = gl_FragCoord.z - dz/32/2/2*2;
        }
        cylinder {
                in vec2 vTexCoords;
                float dz = sqrt(1-abs(vTexCoords.x));
                color = vec4(vec3(dz), 1);
                gl_FragDepth = gl_FragCoord.z - dz/256/2*2;
        }
}

