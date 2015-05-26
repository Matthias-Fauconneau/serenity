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
                vec3 v = vec3(vTexCoords, dz);
                buffer rotationBuffer { vec4[] rotation; };
                vec4 q = rotation[gl_PrimitiveID/2].yzwx;
                vec4 qmul(vec4 p, vec4 q) {
                 return vec4(p.w*q.xyz + q.w*p.xyz + cross(p.xyz, q.xyz), p.w*q.w - dot(p.xyz, q.xyz));
                }
                color = vec4(vec3(dz)*(1+qmul(q, qmul(vec4(v, 0), vec4(-q.xyz, q.w))).xyz)/2, 1);
                gl_FragDepth = gl_FragCoord.z - dz/32/2/2*2;
        }
        cylinder {
                float dz = sqrt(1-abs(vTexCoords.x));
                color = vec4(1./2*vec3(dz), 1);
                gl_FragDepth = gl_FragCoord.z - dz/256/2*2;
        }
        blit {
         uniform sampler2D image;
         color = texture2D(image, vTexCoords);
        }
        flat {
         uniform vec3 uColor;
         color = vec4(uColor,1);
        }
}
