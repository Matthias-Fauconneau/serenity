vertex {
        in vec3 position;
        gl_Position = vec4(position, 1);
        out vec2 vLocalCoords;
        vLocalCoords = vec2[](vec2(-1,-1),vec2(1,-1),vec2(-1,1),
                                           vec2(-1,1),vec2(1,-1),vec2(1,1))[gl_VertexID%6];
}
fragment {
        out vec4 color;
        in vec2 vLocalCoords;
        sphere {
                if(length(vLocalCoords) > 1) discard;
                float dz = sqrt(1-dot(vLocalCoords,vLocalCoords));
                vec3 v = vec3(vLocalCoords, dz);
                buffer rotationBuffer { vec4[] rotation; };
                //buffer colorBuffer { vec4[] aColor; };
                vec4 q = vec4(rotation[gl_PrimitiveID/2].yzwx);
                vec4 qmul(vec4 p, vec4 q) {
                 return vec4(p.w*q.xyz + q.w*p.xyz + cross(p.xyz, q.xyz),
                                    p.w*q.w - dot(p.xyz, q.xyz));
                }
                vec3 mul(vec4 p, vec3 v) {
                  return qmul(p, qmul(vec4(v, 0), vec4(-p.xyz, p.w))).xyz;
                }
                color = /*aColor[gl_PrimitiveID/2]*/vec4(dz*(1+mul(q, v))/2, 1);
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
         color = vec4(texture2D(image, (vLocalCoords+1)/2).rgb, 1);
        }
        flat {
         uniform vec3 uColor;
         color = vec4(uColor,1);
        }
}
