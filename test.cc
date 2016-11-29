#include "thread.h"
#include "image.h"
#include "sphere.h"
#include "png.h"

struct Test {
    Test() {
        const uint N = 512;
        Image image (N);
        for(uint vIndex: range(N)) for(uint uIndex: range(N)) {
            const Vec<v8sf, 3> XYZ = hemisphere(uIndex/((N-1)/2.f)-1, vIndex/((N-1)/2.f)-1);
            /*const Vec<v8sf, 2> UV = square(XYZ._[0], XYZ._[1], XYZ._[2]);
            const uint uIndex2 = ((UV._[0]+1)*((N-1)/2.f))[0];
            const uint vIndex2 = ((UV._[1]+1)*((N-1)/2.f))[0];*/
            extern uint8 sRGB_forward[0x1000];
            const float x = XYZ._[0][0], y = XYZ._[1][0], z = XYZ._[2][0];
            const uint B = clamp(0u, uint((x+1)/2*0xFFF), 0xFFFu);
            const uint G = clamp(0u, uint((y+1)/2*0xFFF), 0xFFFu);
            const uint R = clamp(0u, uint((z+1)/2*0xFFF), 0xFFFu);
            image(uIndex, vIndex) = byte4(sRGB_forward[B], sRGB_forward[G], sRGB_forward[R], 0xFF);
            /*assert_((uIndex == uIndex2 && vIndex == vIndex2) || (uIndex == 0 && vIndex==N-1-vIndex2) || (vIndex == 0 && uIndex==N-1-uIndex2),
                    "UVi", uIndex, vIndex,
                    "UV", uIndex/((N-1)/2.f)-1, vIndex/((N-1)/2.f)-1,
                    "XYZ", XYZ._[0][0], XYZ._[1][0], XYZ._[2][0],
                    "UV", UV._[0][0], UV._[1][0],
                    "UVi", uIndex2, vIndex2);*/

        }
        writeFile("image.png", encodePNG(image), currentWorkingDirectory(), true);
    }
} app;
