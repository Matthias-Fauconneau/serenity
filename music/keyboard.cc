#include "keyboard.h"
#include "display.h"

void Keyboard::inputNoteEvent(uint key, uint vel) {
    if(vel) { if(!input.contains(key)) input << key; } else input.removeAll(key); contentChanged();
}
void Keyboard::midiNoteEvent(uint key, uint vel) {
    if(vel) { if(!midi.contains(key)) midi << key; } else midi.removeAll(key); contentChanged();
}

void Keyboard::render(int2 position, int2 size) {
    int y0 = position.y;
    int y1 = y0+size.y*2/3;
    int y2 = y0+size.y;
    int dx = round(size.x/88.f);
    int margin = (size.x-88*dx)/2;
    for(uint key=0; key<88; key++) {
        vec4 white = midi.contains(key+21)?red:input.contains(key+21)?blue: ::white;
        int x0 = position.x + margin + key*dx;
        int x1 = x0 + dx;
        line(int2(x0, y0), int2(x0, y1-1), black);

        int notch[12] = { 3, 1, 4, 0, 1, 2, 1, 4, 0, 1, 2, 1 };
        int l = notch[key%12], r = notch[(key+1)%12];
        if(key==0) l=0; //A-1 has no left notch
        if(l==1) { // black key
            line(int2(x0, y1-1), int2(x1, y1-1), black);
            fill(x0+1,y0, x1+1,y1-1, midi.contains(key+21)?red:input.contains(key+21)?blue: ::black);
        } else {
            fill(x0+1,y0, x1,y2, white); // white key
            line(int2(x0-l*dx/6, y1-1), int2(x0-l*dx/6, y2), black); //left edge
            fill(x0+1-l*dx/6,y1, x1,y2, white); //left notch
            if(key!=87) fill(x1,y1, x1-1+(6-r)*dx/6,y2, white); //right notch
            //right edge will be next left edge
        }
        if(key==87) { //C7 has no right notch
            line(int2(x1+dx/2, y0), int2(x1+dx/2, y2), black);
            fill(x1,y0, x1+dx/2,y1-1, white);
        }
    }
}

bool Keyboard::mouseEvent(int2 cursor, int2 size, Event event, Button) {
    uint key = 21+88*cursor.x/size.x;
    if(event==Press) noteEvent(key, 100);
    if(event==Release) noteEvent(key, 0);
    return true;
}
