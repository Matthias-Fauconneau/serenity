vertex {
        in vec3 position;
        gl_Position = vec4(position, 1);
        out vec2 vLocalCoords;
        vLocalCoords = vec2[](vec2(-1,-1),vec2(1,-1),vec2(-1,1),vec2(-1,1),vec2(1,-1),vec2(1,1))[gl_VertexID%6];
}
fragment {
        out vec4 color;
        in vec2 vLocalCoords;
        sphere {
                if(length(vLocalCoords) > 1) discard;
                float dz = sqrt(1-dot(vLocalCoords,vLocalCoords));
                vec3 v = vec3(vLocalCoords, dz);
                buffer rotationBuffer { vec4[] rotation; };
                vec4 q = rotation[gl_PrimitiveID/2].yzwx;
                vec4 qmul(vec4 p, vec4 q) {
                 return vec4(p.w*q.xyz + q.w*p.xyz + cross(p.xyz, q.xyz), p.w*q.w - dot(p.xyz, q.xyz));
                }
                color = vec4(vec3(dz)*(1+qmul(q, qmul(vec4(v, 0), vec4(-q.xyz, q.w))).xyz)/2, 1);
                uniform float radius;
                gl_FragDepth = gl_FragCoord.z - dz * radius;
        }
        cylinder {
                float dz = sqrt(1-abs(vLocalCoords.x));
                color = vec4(1./2*vec3(dz), 1);
                uniform float radius;
                gl_FragDepth = gl_FragCoord.z - dz * radius;
        }
        blit {
         uniform sampler2D image;
         color = texture2D(image, (vLocalCoords+1)/2);
        }
        flat {
         uniform vec3 uColor;
         color = vec4(uColor,1);
        }
}
